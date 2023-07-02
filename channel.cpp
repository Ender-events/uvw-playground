#include <chrono>
#include <coroutine>
#include <cstddef>
#include <deque>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>

#include "lazy.hh"
#include <uvw.hpp>

template <typename Derived>
class IntrusiveNode {
public:
    Derived* next = nullptr;
};

template <typename T>
class FIFOList {
public:
    FIFOList()
        : head(nullptr)
        , tail(nullptr)
    {
    }

    void push(T* newNode)
    {
        if (tail == nullptr) {
            head = newNode;
            tail = newNode;
        } else {
            tail->next = newNode;
            tail = newNode;
        }
    }

    auto pop() -> T*
    {
        if (head == nullptr) {
            return nullptr;
        }

        T* elem = head;
        head = head->next;
        if (head == nullptr) {
            // The list becomes empty after the pop
            tail = nullptr;
        }

        return elem;
    }

    auto empty() -> bool
    {
        return head == nullptr;
    }

private:
    T* head;
    T* tail;
};

template <typename Type>
class channel {
public:
    channel(uvw::loop& loop, std::size_t bufferSize = 0);

    struct async_recv : public IntrusiveNode<async_recv> {
        async_recv(channel<Type>& channel)
            : channel_{channel}
        {
        }

        [[nodiscard]] auto await_ready() const -> bool
        {
            return !channel_.fifo_.empty() || !channel_.senders_.empty();
        }
        auto await_suspend(std::coroutine_handle<> handle) -> std::coroutine_handle<>
        {
            handle_ = handle;
            channel_.receivers_.push(this);
            return std::noop_coroutine();
        }
        auto await_resume()
        {
            if (!channel_.fifo_.empty()) {
                Type data = std::move(channel_.fifo_.front());
                channel_.fifo_.pop_front();
                return std::make_tuple(std::move(data), true);
            }
            if (!channel_.senders_.empty()) {
                auto send = channel_.senders_.pop();
                Type data = std::move(send->data_.value());
                send->data_.reset();
                channel_.consumeds_.push(send);
                return std::make_tuple(std::move(data), true);
            }
            if (channel_.open_) {
                throw std::runtime_error("unexpected await resume");
            }
            return std::make_tuple(Type{}, false);
        }

        channel<Type>& channel_;
        std::coroutine_handle<> handle_{};
    };

    struct async_send : public IntrusiveNode<async_send> {
        async_send(channel<Type>& channel, Type&& data)
            : channel_{channel}
            , data_{std::move(data)}
        {
        }

        auto await_ready() -> bool
        {
            if (channel_.full()) {
                return false;
            }
            channel_.fifo_.push_back(std::move(data_.value()));
            data_.reset();
            return true;
        }
        auto await_suspend(std::coroutine_handle<> handle) -> std::coroutine_handle<>
        {
            handle_ = handle;
            channel_.senders_.push(this);
            if (!channel_.receivers_.empty()) {
                auto recv = channel_.receivers_.pop();
                return recv->handle_;
            }
            return std::noop_coroutine();
        }
        void await_resume()
        {
        }
        channel<Type>& channel_;
        std::optional<Type> data_;
        std::coroutine_handle<> handle_{};
    };

    async_recv recv()
    {
        async_recv res{*this};
        handleSend_->send();
        return res;
    }
    // TODO: c++23 deducing this ?
    async_send send(const Type& type);
    async_send send(Type&& type);
    void close();

    void sync_await()
    {
        while ((!open_ || !fifo_.empty()) && !receivers_.empty()) {
            auto recv = receivers_.pop();
            recv->handle_.resume();
        }
        while (!consumeds_.empty()) {
            auto send = consumeds_.pop();
            send->handle_.resume();
        }
        while ((!open_ || !full()) && !senders_.empty()) {
            auto send = senders_.pop();
            send->handle_.resume();
        }
    }

private:
    FIFOList<async_recv> receivers_{};
    FIFOList<async_send> senders_{};
    FIFOList<async_send> consumeds_{};
    std::mutex m_{};
    std::deque<Type> fifo_{};
    std::shared_ptr<uvw::async_handle> handleRecv_;
    std::shared_ptr<uvw::async_handle> handleSend_;
    bool open_ = true;
    std::size_t bufferSize_;

    auto full() -> bool
    {
        return fifo_.size() >= bufferSize_;
    }
};

template <typename Type>
channel<Type>::channel(uvw::loop& loop, std::size_t bufferSize)
    : handleRecv_{loop.resource<uvw::async_handle>()}
    , handleSend_{loop.resource<uvw::async_handle>()}
    , bufferSize_{bufferSize}
{
    handleRecv_->on<uvw::async_event>([this](const uvw::async_event&, const uvw::async_handle&) {
        while ((!open_ || !fifo_.empty()) && !receivers_.empty()) {
            auto recv = receivers_.pop();
            recv->handle_.resume();
        }
        if (!open_) {
            std::cout << "handleRecv_ closed\n";
            handleRecv_->close();
        }
    });
    handleSend_->on<uvw::async_event>([this](const uvw::async_event&, const uvw::async_handle&) {
        while (!consumeds_.empty()) {
            auto send = consumeds_.pop();
            send->handle_.resume();
        }
        while ((!open_ || !full()) && !senders_.empty()) {
            auto send = senders_.pop();
            send->handle_.resume();
        }
        if (!open_) {
            std::cout << "handleSend_ closed\n";
            handleSend_->close();
        }
    });
}

template <typename Type>
typename channel<Type>::async_send channel<Type>::send(const Type& type)
{
    std::scoped_lock lock{m_};
    if (!open_) {
        throw std::invalid_argument{"send on closed channel"};
    }
    async_send res{*this, Type{type}};
    handleRecv_->send();
    return res;
}

template <typename Type>
typename channel<Type>::async_send channel<Type>::send(Type&& type)
{
    std::scoped_lock lock{m_};
    if (!open_) {
        throw std::invalid_argument{"send on closed channel"};
    }
    async_send res{*this, std::move(type)};
    handleRecv_->send();
    return res;
}

template <typename Type>
void channel<Type>::close()
{
    open_ = false;
    handleRecv_->send();
    handleSend_->send();
}

auto tick(std::shared_ptr<channel<int>> tick, std::shared_ptr<channel<int>> tack) -> std::lazy<void>
{
    while (true) {
        auto&& [a, ok] = co_await tick->recv();
        if (!ok) {
            tack->close();
            std::cout << "tack closed\n";
            break;
        }
        std::cout << "tick (" << std::this_thread::get_id() << "): " << a << '\n';
        co_await tack->send(a);
    }
}

auto tack(std::shared_ptr<channel<int>> tick, std::shared_ptr<channel<int>> tack) -> std::lazy<void>
{
    co_await tick->send(0);
    while (true) {
        auto&& [a, ok] = co_await tack->recv();
        if (!ok) {
            break;
        }
        std::cout << "tack (" << std::this_thread::get_id() << "): " << a << '\n';
        ++a;
        if (a < 10) {
            co_await tick->send(a);
        } else {
            tick->close();
            std::cout << "tick closed\n";
        }
    }
}

int main()
{
    auto loop = uvw::loop::get_default();
    auto chan_tick = std::make_shared<channel<int>>(*loop);
    auto chan_tack = std::make_shared<channel<int>>(*loop);
    auto lazy_tick = tick(chan_tick, chan_tack);
    auto lazy_tack = tack(chan_tick, chan_tack);
    std::cout << "sync_await lazy_tick\n";
    lazy_tick.sync_await();
    std::cout << "sync_await lazy_tack\n";
    lazy_tack.sync_await();

    std::cout << "begin run loop\n";
    loop->run();
    std::cout << "end\n";
}

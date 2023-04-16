#include <chrono>
#include <cstddef>
#include <iostream>
#include <list>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <uvw.hpp>

template <typename Type>
class Channel {
public:
    Channel(uvw::loop&);
    using ReadCb = std::function<void(Type&&)>;
    using CloseCb = std::function<void()>;

    // TODO: c++23 deducing this ?
    void send(const Type& type);
    void send(Type&& type);
    void onRead(ReadCb&& cb)
    {
        read_cb_ = std::move(cb);
    }
    void onClose(CloseCb&& cb)
    {
        close_cb_ = std::move(cb);
    }
    void close();

private:
    std::list<Type> fifo_{};
    std::mutex m_{};
    std::shared_ptr<uvw::async_handle> async_;
    ReadCb read_cb_;
    CloseCb close_cb_;
    bool open_ = true;
};

template <typename Type>
Channel<Type>::Channel(uvw::loop& loop)
    : async_{loop.resource<uvw::async_handle>()}
{
    async_->on<uvw::async_event>([this](const uvw::async_event&, const uvw::async_handle&) {
        if (!read_cb_)
            return;
        std::scoped_lock lock{m_};
        if (open_) {
            do {
                read_cb_(std::move(fifo_.front()));
                fifo_.pop_front();
            } while (!fifo_.empty());
        } else {
            async_->close();
            if (close_cb_)
                close_cb_();
        }
    });
}

template <typename Type>
void Channel<Type>::send(const Type& type)
{
    std::scoped_lock lock{m_};
    fifo_.push_back(type);
    async_->send();
}

template <typename Type>
void Channel<Type>::send(Type&& type)
{
    std::scoped_lock lock{m_};
    fifo_.push_back(std::move(type));
    async_->send();
}

template <typename Type>
void Channel<Type>::close()
{
    open_ = false;
    async_->send();
}

int main()
{
    auto loop = uvw::loop::get_default();
    auto tick = Channel<int>(*loop);
    auto tack = Channel<int>(*loop);

    tick.onRead([&tack](int i) {
        std::cout << "tick (" << std::this_thread::get_id() << "): " << i << '\n';
        std::this_thread::sleep_for(std::chrono::milliseconds{500});
        i++;
        tack.send(i);
    });

    tick.onClose([&tack]() {
        tack.close();
    });

    tack.onRead([&tick](int i) {
        std::cout << "tack (" << std::this_thread::get_id() << "): " << i << '\n';
        std::this_thread::sleep_for(std::chrono::milliseconds{500});
        i++;
        if (i < 10)
            tick.send(i);
        else
            tick.close();
    });

    std::cout << "begin\n";
    tick.send(0);
    loop->run();
    std::cout << "end\n";
}

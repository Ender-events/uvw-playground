#pragma once

#include <coroutine>
#include <memory>

#include <stdexcept>
#include <uvw/loop.h>
#include <uvw/timer.h>

class async_sleep {
public:
    using time = uvw::timer_handle::time;
    async_sleep(time timeout, const std::shared_ptr<uvw::loop>& loop = uvw::loop::get_default())
        : timeout_{timeout}
        , timer_{loop->resource<uvw::timer_handle>()}
    {
        timer_->on<uvw::error_event>([](auto&&...) { throw std::runtime_error{"unexpected error event"}; });
        timer_->on<uvw::timer_event>([this](const auto&, auto& handle) {
            handle.close();
            handle_.resume();
        });
    }

    [[nodiscard]] auto await_ready() const -> bool
    {
        return false;
    }
    void await_suspend(std::coroutine_handle<> handle)
    {
        handle_ = handle;
        timer_->start(timeout_, time{});
    }
    void await_resume()
    {
    }

private:
    time timeout_;
    std::shared_ptr<uvw::timer_handle> timer_;
    std::coroutine_handle<> handle_{};
};

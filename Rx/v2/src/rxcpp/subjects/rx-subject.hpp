// Copyright (c) Microsoft Open Technologies, Inc. All rights reserved. See License.txt in the project root for license information.

#pragma once

#if !defined(RXCPP_RX_SCHEDULER_SUBJECT_HPP)
#define RXCPP_RX_SCHEDULER_SUBJECT_HPP

#include "../rx-includes.hpp"

namespace rxcpp {

namespace subjects {

namespace detail {

template<class T>
class multicast_observer
    : public observer_base<T>
{
    typedef observer_base<T> base;
    typedef subscriber<T> observer_type;
    typedef std::vector<observer_type> list_type;

    struct mode
    {
        enum type {
            Invalid = 0,
            Casting,
            Completed,
            Errored
        };
    };

    struct completer_type;

    struct state_type
        : public std::enable_shared_from_this<state_type>
    {
        state_type()
            : current(mode::Casting)
        {
        }
        std::atomic<int> generation;
        std::mutex lock;
        typename mode::type current;
        std::exception_ptr error;
    };

    struct completer_type
        : public std::enable_shared_from_this<completer_type>
    {
        ~completer_type()
        {
        }
        completer_type(std::shared_ptr<state_type> s, const std::shared_ptr<completer_type>& old, observer_type o)
            : state(s)
        {
            if (old) {
                observers.reserve(old->observers.size() + 1);
                std::copy_if(
                    old->observers.begin(), old->observers.end(),
                    std::inserter(observers, observers.end()),
                    [](const observer_type& o){
                        return o.is_subscribed();
                    });
            }
            observers.push_back(o);
        }
        std::shared_ptr<state_type> state;
        list_type observers;
    };

    // this type prevents a circular ref between state and completer
    struct binder_type
        : public std::enable_shared_from_this<binder_type>
    {
        explicit binder_type(composite_subscription cs)
            : state(std::make_shared<state_type>())
            , current_generation(0)
            , lifetime(cs)
        {
        }

        std::shared_ptr<state_type> state;

        // used to avoid taking lock in on_next
        mutable int current_generation;
        mutable std::shared_ptr<completer_type> current_completer;

        // must only be accessed under state->lock
        mutable std::shared_ptr<completer_type> completer;

        composite_subscription lifetime;
    };

    std::shared_ptr<binder_type> b;



public:
    explicit multicast_observer(composite_subscription cs)
        : b(std::make_shared<binder_type>(cs))
    {
    }
    bool has_observers() const {
        return b->current_completer && !b->current_completer->observers.empty();
    }
    void add(observer_type o) const {
        std::unique_lock<std::mutex> guard(b->state->lock);
        switch (b->state->current) {
        case mode::Casting:
            {
                if (o.is_subscribed()) {
                    b->completer = std::make_shared<completer_type>(b->state, b->completer, o);
                    ++b->state->generation;
                }
            }
            break;
        case mode::Completed:
            {
                guard.unlock();
                o.on_completed();
                return;
            }
            break;
        case mode::Errored:
            {
                auto e = b->state->error;
                guard.unlock();
                o.on_error(e);
                return;
            }
            break;
        default:
            abort();
        }
    }
    template<class V>
    void on_next(V&& v) const {
        if (b->current_generation != b->state->generation) {
            std::unique_lock<std::mutex> guard(b->state->lock);
            if (!b->completer) {
                return;
            }
            b->current_generation = b->state->generation;
            b->current_completer = b->completer;
        }
        if (!b->current_completer || b->current_completer->observers.empty()) {
            return;
        }
        for (auto& o : b->current_completer->observers) {
            if (o.is_subscribed()) {
                o.on_next(std::forward<V>(v));
            }
        }
    }
    void on_error(std::exception_ptr e) const {
        std::unique_lock<std::mutex> guard(b->state->lock);
        if (b->state->current == mode::Casting) {
            b->state->error = e;
            b->state->current = mode::Errored;
            auto c = std::move(b->completer);
            guard.unlock();
            if (c) {
                for (auto& o : c->observers) {
                    if (o.is_subscribed()) {
                        o.on_error(e);
                    }
                }
            }
            b->lifetime.unsubscribe();
        }
    }
    void on_completed() const {
        std::unique_lock<std::mutex> guard(b->state->lock);
        if (b->state->current == mode::Casting) {
            b->state->current = mode::Completed;
            auto c = std::move(b->completer);
            guard.unlock();
            if (c) {
                for (auto& o : c->observers) {
                    if (o.is_subscribed()) {
                        o.on_completed();
                    }
                }
            }
            b->lifetime.unsubscribe();
        }
    }
};


}

template<class T>
class subject
{
    composite_subscription lifetime;
    detail::multicast_observer<T> s;

public:
    explicit subject(composite_subscription cs = composite_subscription())
        : lifetime(cs)
        , s(cs)
    {
    }

    bool has_observers() const {
        return s.has_observers();
    }

    subscriber<T, observer<T, detail::multicast_observer<T>>> get_subscriber(composite_subscription cs = composite_subscription()) const {
        auto lt = lifetime;
        auto token = lt.add(cs);
        cs.add(make_subscription([token, lt](){lt.remove(token);}));
        return make_subscriber<T>(cs, observer<T, detail::multicast_observer<T>>(s));
    }

    observable<T> get_observable() const {
        return make_dynamic_observable<T>([this](subscriber<T> o){
            this->s.add(std::move(o));
        });
    }
};

}

}

#endif

#include "../test.h"
#include "rxcpp/rx.hpp"
#include "rxcpp/operators/rx-observe_on.hpp"
#include "rxcpp/operators/rx-merge.hpp"
#include "rxcpp/rx-scheduler.hpp"

SCENARIO("race condition") {

  for (std::size_t i=0; i < 5000; ++i) {
    auto comp1 = rxcpp::composite_subscription();
    auto mco = rxcpp::subjects::detail::multicast_observer<std::string>(comp1);

    auto comp2 = rxcpp::composite_subscription();
    auto obs = rxcpp::observer<std::string>();
    auto sub = rxcpp::subscriber<std::string>(
      rxcpp::trace_id::make_next_id_subscriber(),
      comp2,
      obs);

    using namespace std::chrono_literals;
    auto t = std::thread([&](){
      comp2.unsubscribe();
    });

    mco.add(mco.get_subscription(), sub);
    t.join();
  }
}

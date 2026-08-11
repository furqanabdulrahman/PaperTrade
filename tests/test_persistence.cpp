//
// test_persistence.cpp — portfolio save/load round-trip (session persistence).
//
#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <string>

#include "papertrade/domain/Portfolio.h"
#include "papertrade/services/Persistence.h"

using namespace papertrade;

TEST_CASE("Portfolio survives a save/load round-trip", "[persistence]") {
    const std::string path = "pt_persist_test.json";
    std::remove(path.c_str());

    {
        Portfolio p(50000.0);
        p.buy("AAPL", 10, 100.0);          // cash 49000
        p.buy("MSFT", 5, 200.0);           // cash 48000
        p.sell("AAPL", 4, 130.0);          // cash 48520, realized +120
        p.placeLimit("NVDA", Side::Buy, 3, 90.0);
        REQUIRE(savePortfolio(p, path));
    }

    Portfolio loaded;  // starts at default cash
    REQUIRE(loadPortfolio(loaded, path));

    REQUIRE(loaded.cash() == 48520.0);
    REQUIRE(loaded.position("AAPL")->qty == 6);
    REQUIRE(loaded.position("AAPL")->avgCost == 100.0);
    REQUIRE(loaded.position("MSFT")->qty == 5);
    REQUIRE(loaded.realizedPnl() == 120.0);
    REQUIRE(loaded.orderCount() == 3);
    REQUIRE(loaded.pendingCount() == 1);
    REQUIRE(loaded.pendingAt(0).ticker == "NVDA");

    // A restored limit order still fills when its trigger is met.
    auto filled = loaded.evaluate([](const std::string&) { return 80.0; });
    REQUIRE(filled.size() == 1);
    REQUIRE(loaded.position("NVDA")->qty == 3);

    std::remove(path.c_str());
}

TEST_CASE("loadPortfolio leaves the portfolio untouched on a missing file",
          "[persistence]") {
    Portfolio p(12345.0);
    REQUIRE_FALSE(loadPortfolio(p, "definitely_not_here_98765.json"));
    REQUIRE(p.cash() == 12345.0);
}

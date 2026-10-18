// C++ runtime check: libc++ (static), exceptions, RTTI, threads, containers, strings, lambdas.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#define STEP(n, cond) do { if (!(cond)) return (n); } while (0)

namespace {
struct Base { virtual ~Base() = default; virtual int id() const { return 1; } };
struct Derived : Base { int id() const override { return 2; } };
struct Counter { static int live; Counter() { live++; } ~Counter() { live--; } };
int Counter::live = 0;
static Counter g_static_counter; // dynamic initialization at load
thread_local int t_counter = 5;
}

extern "C" int t_cxx_basic() {
    STEP(1, Counter::live == 1); // static initializer ran
    std::vector<int> v(100);
    std::iota(v.begin(), v.end(), 1);
    STEP(2, std::accumulate(v.begin(), v.end(), 0) == 5050);
    std::sort(v.begin(), v.end(), std::greater<int>());
    STEP(3, v.front() == 100 && v.back() == 1);
    std::string s = "henge";
    s += " " + std::to_string(42) + "!";
    STEP(4, s == "henge 42!");
    std::map<std::string, int> m{{"a", 1}, {"b", 2}};
    m["c"] = 3;
    STEP(5, m.size() == 3 && m.at("b") == 2);
    std::unordered_map<int, std::string> um;
    for (int i = 0; i < 1000; i++) um[i] = std::to_string(i);
    STEP(6, um.size() == 1000 && um[999] == "999");
    std::ostringstream os;
    os << "x=" << 7 << " y=" << 2.5;
    STEP(7, os.str() == "x=7 y=2.5");
    auto up = std::make_unique<Derived>();
    Base *b = up.get();
    STEP(8, b->id() == 2 && dynamic_cast<Derived *>(b) != nullptr);
    std::function<int(int)> f = [k = 10](int x) { return x + k; };
    STEP(9, f(5) == 15);
    return 0;
}

extern "C" int t_cxx_exceptions() {
    try {
        throw std::runtime_error("boom");
    } catch (const std::exception &e) {
        STEP(1, std::string(e.what()) == "boom");
    }
    int caught = 0;
    try {
        try { throw 42; } catch (int x) { caught = x; throw std::string("rethrown"); }
    } catch (const std::string &s) {
        STEP(2, s == "rethrown" && caught == 42);
    }
    try {
        std::vector<int> v;
        v.at(3);
    } catch (const std::out_of_range &) {
        return 0;
    }
    return 3;
}

extern "C" int t_cxx_threads() {
    std::atomic<int> sum{0};
    std::vector<std::thread> ts;
    for (int i = 1; i <= 4; i++) ts.emplace_back([&sum, i] { for (int k = 0; k < 1000; k++) sum += i; });
    for (auto &t : ts) t.join();
    STEP(1, sum == 10000);

    std::mutex m;
    std::condition_variable cv;
    bool ready = false;
    int value = 0;
    std::thread producer([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        { std::lock_guard<std::mutex> lk(m); value = 7; ready = true; }
        cv.notify_one();
    });
    {
        std::unique_lock<std::mutex> lk(m);
        STEP(2, cv.wait_for(lk, std::chrono::seconds(2), [&] { return ready; }));
    }
    producer.join();
    STEP(3, value == 7);

    int tls_in_thread = 0;
    std::thread([&] { t_counter += 1; tls_in_thread = t_counter; }).join();
    STEP(4, tls_in_thread == 6 && t_counter == 5);

    auto t0 = std::chrono::steady_clock::now();
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    auto dt = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    STEP(5, dt >= 25 && dt < 500);
    return 0;
}

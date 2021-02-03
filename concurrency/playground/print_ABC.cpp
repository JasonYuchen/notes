#include <thread>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <iostream>
#include <vector>
#include <queue>
#include <chrono>
#include <future>

using namespace std;

// there are 3 threads printing 'A', 'B' and 'C', respectively
// we want them to print in order like 'ABCABCABC'
constexpr int group = 3;
constexpr int rounds = 3;

void sleep()
{
    // drawback ? pitfall ?
    vector<thread> threads;
    for (int i = 0; i < group; ++i) {
        threads.emplace_back([i, cnt=rounds] () mutable {
            while (cnt--) {
                cout << char('A'+ i) << endl;
                 // precision ? may drift over time and the order cannot be guaranteed
                this_thread::sleep_for(chrono::milliseconds(500));
            }
        });
        // context switch here
        this_thread::sleep_for(chrono::milliseconds(100));
    }
    for (auto &t : threads) {
        t.join();
    }
}

void cv()
{
    mutex mtx;
    condition_variable cv;
    int turn = -1;
    vector<thread> threads;
    for (int i = 0; i < group; ++i) {
        threads.emplace_back([&mtx, &cv, &turn, i] () mutable {
            while (true) {
                {
                    // why we need mutex here ?
                    unique_lock<mutex> guard(mtx);
                    cv.wait(
                        guard, 
                        [&turn, i]() { return turn % group == i || turn >= group * rounds; });
                    if (turn >= group * rounds) {
                        break;
                    }
                    turn++;
                    cout << char('A'+i) << endl;
                }
                // what if use `cv.notify_one()` ?
                cv.notify_all();
            }
        });
    }
    {
        lock_guard<mutex> guard(mtx);
        turn++; 
    }
    // it seems use `cv.notify_one()` is also ok here, why ?
    cv.notify_all();
    for (auto &t : threads) {
        t.join();
    }
}

void cv_careful()
{
    mutex mtx;
    condition_variable cvs[group];
    int turn = -1;
    vector<thread> threads;
    for (int i = 0; i < group; ++i) {
        threads.emplace_back([&mtx, &cvs, &turn, i]() mutable {
            while (true) {
                {
                    unique_lock<mutex> guard(mtx);
                    cvs[i].wait(
                        guard,
                        [&turn, i]() { return turn % group == i || turn >= group * rounds; });
                    if (turn >= group * rounds) {
                        // inside the guard scope, is this ok?
                        cvs[(i+1) % group].notify_one();
                        break;
                    }
                    turn++;
                    cout << char('A'+i) << endl;
                }
                cvs[(i+1) % group].notify_one();
            }
        });
    }
    {
        lock_guard<mutex> guard(mtx);
        turn++; 
    }
    cvs[0].notify_one();
    for (auto &t : threads) {
        t.join();
    }
}

// naive thread-safe queue
class myqueue {
public:
    future<void> push(function<void()> task) {
        unique_lock<mutex> guard(mtx);
        auto t = packaged_task<void()>{task};
        auto f = t.get_future();
        q.push(std::move(t));
        cv.notify_all();
        return f;
    }
    packaged_task<void()> pop() {
        unique_lock<mutex> guard(mtx);
        cv.wait(guard, [this](){ return !q.empty(); });
        auto ch = std::move(q.front());
        q.pop();
        return ch;
    }
private:
    mutex mtx;
    condition_variable cv;
    queue<packaged_task<void()>> q;
};

void actor()
{
    atomic_bool stop{false};
    vector<thread> threads;
    vector<myqueue> queues(group);
    for (int i = 0; i < group; ++i) {
        threads.emplace_back([&stop, &queues, i]() mutable {
            while (!stop.load()) {
                queues[i].pop()();
            }
        });
    }
    for (int i = 0; i < rounds; ++i) {
        for (int j = 0; j < group; ++j) {
            auto f = queues[j].push([j](){ cout << char('A'+j) << endl; });
            f.wait();
        }
    }
    stop.store(true);
    for (auto &q : queues) {
        q.push([](){});
    }
    for (auto &t : threads) {
        t.join();
    }
}

int main(int argc, char **argv)
{
    actor();
    return 0;
}
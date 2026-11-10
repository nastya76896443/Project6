#include "rolling_average.h"

#include <charconv>
#include <cmath>
#include <coroutine>
#include <cstdlib>
#include <iomanip>
#include <istream>
#include <limits>
#include <ostream>
#include <string>
#include <system_error>

namespace {

constexpr int windowSize = 10;

struct AverageTask {
    struct promise_type;
    using Handle = std::coroutine_handle<promise_type>;

    struct promise_type {
        int exitCode = EXIT_SUCCESS;
        bool failedWithException = false;

        AverageTask get_return_object() noexcept
        {
            return AverageTask{Handle::from_promise(*this)};
        }

        std::suspend_never initial_suspend() const noexcept { return {}; }
        std::suspend_always final_suspend() const noexcept { return {}; }
        void return_value(int code) noexcept { exitCode = code; }

        void unhandled_exception() noexcept
        {
            exitCode = EXIT_FAILURE;
            failedWithException = true;
        }
    };

    Handle handle;

    explicit AverageTask(Handle h) noexcept : handle(h) {}
    AverageTask(const AverageTask&) = delete;
    AverageTask& operator=(const AverageTask&) = delete;
    ~AverageTask() { handle.destroy(); }
};

struct PauseOnTriple {
    const double* numbers;
    int count;

bool await_ready() const noexcept
{
    if (count < 3) {
        return true;
    }

    if (numbers[count - 1] == numbers[count - 2]
        && numbers[count - 2] == numbers[count - 3]) {
        return false;
    }

    return true;
}

    void await_suspend(std::coroutine_handle<>) const noexcept {}
    void await_resume() const noexcept {}
};

bool parseNumber(const std::string& token, double& number)
{
    const char* first = token.data();
    const char* end = first + token.size();

    if (first != end && *first == '+') {
        ++first;
        if (first == end || *first == '-') {
            return false;
        }
    }

    const auto result = std::from_chars(first, end, number);
    return result.ec == std::errc{} && result.ptr == end && std::isfinite(number);
}

long double averageOf(const double numbers[])
{
    // Divide by a power of two only if adding large numbers could overflow.
    const long double limit = std::numeric_limits<long double>::max() / 16.0L;
    long double scale = 1.0L;
    for (int i = 0; i < windowSize; ++i) {
        if (std::abs(numbers[i]) > limit) {
            scale = 16.0L;
            break;
        }
    }

    long double sum = 0;
    long double correction = 0;
    for (int i = 0; i < windowSize; ++i) {
        const long double number = numbers[i] / scale;
        const long double next = sum + number;

        // Keep the rounding error from this addition separately.
        if (std::abs(sum) >= std::abs(number)) {
            correction += (sum - next) + number;
        } else {
            correction += (number - next) + sum;
        }
        sum = next;
    }

    return (sum + correction) / (windowSize / scale);
}

AverageTask readNumbers(std::istream& input, std::ostream& output, std::ostream& errors)
{
    double numbers[windowSize]{};
    int count = 0;
    std::string token;

    while (input >> token) {
        double number = 0;
        if (!parseNumber(token, number)) {
            errors << "Input error: expected a finite decimal number.\n";
            co_return EXIT_FAILURE;
        }

        if (count < windowSize) {
            numbers[count] = number;
            ++count;
        } else {
            for (int i = 1; i < windowSize; ++i) {
                numbers[i - 1] = numbers[i];
            }
            numbers[windowSize - 1] = number;
        }

        co_await PauseOnTriple{numbers, count};

        if (count == windowSize) {
            output << "Average: " << std::setprecision(12) << averageOf(numbers) << '\n';
        }
    }

    if (input.bad() || !input.eof()) {
        errors << "Input error: could not read from the stream.\n";
        co_return EXIT_FAILURE;
    }

    output << "Done: end of input.\n";
    co_return EXIT_SUCCESS;
}

} // namespace

int runRollingAverage(std::istream& input, std::ostream& output, std::ostream& errors)
{
    auto task = readNumbers(input, output, errors);

    while (!task.handle.done()) {
        output << "Paused: last three values are equal. Resuming...\n";
        task.handle.resume();
    }

    const auto& promise = task.handle.promise();
    if (promise.failedWithException) {
        errors << "Error: coroutine could not finish.\n";
    }
    return promise.exitCode;
}


#include "thread_pool_helper.h"

#include <atomic>
#include <latch>
#include <memory>
#include <string>

#include <boost/asio/post.hpp>

#if defined(__linux__)
#include <pthread.h>
#endif

namespace
{

#if defined(__linux__)

constexpr std::size_t kLinuxThreadNameMaxLength = 15;

std::string makeThreadName(const std::string& baseName, std::size_t index)
{
    std::string suffix = "_" + std::to_string(index + 1);

    if (baseName.size() + suffix.size() <= kLinuxThreadNameMaxLength)
    {
        return baseName + suffix;
    }

    const std::size_t availableBaseLength = kLinuxThreadNameMaxLength - suffix.size();

    return baseName.substr(0, availableBaseLength) + suffix;
}

#endif

} // namespace

std::unique_ptr<boost::asio::thread_pool>
ThreadPoolHelper::create(std::size_t threadCount, const std::string& baseName)
{
    if (threadCount == 0)
    {
        return nullptr;
    }

    auto pool = std::make_unique<boost::asio::thread_pool>(threadCount);

#if defined(__linux__)

    /*
     * One naming task is posted for every worker.
     *
     * Each task blocks on the latch after naming itself.
     * Therefore one worker cannot execute multiple naming
     * tasks while another worker remains unnamed.
     */
    auto startupLatch =
        std::make_shared<std::latch>(static_cast<std::ptrdiff_t>(threadCount));

    for (std::size_t i = 0; i < threadCount; ++i)
    {
        const std::string threadName = makeThreadName(baseName, i);

        boost::asio::post(
            *pool,
            [
                startupLatch,
                threadName
            ]()
            {
                const int result =
                    ::pthread_setname_np(
                        ::pthread_self(),
                        threadName.c_str());

                /*
                 * Thread naming is diagnostic only.
                 * Failure must not stop the worker pool.
                 */
                (void)result;

                startupLatch->count_down();

                /*
                 * Do not let this worker take another
                 * naming task until every pool worker
                 * has executed one naming task.
                 */
                startupLatch->wait();
            });
    }

    /*
     * Do not return the pool until all workers have
     * executed their naming tasks.
     */
    startupLatch->wait();

#else

    (void)baseName;

#endif

    return pool;
}
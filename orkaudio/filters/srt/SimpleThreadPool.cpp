#include <boost/asio.hpp>
#include <thread>
#include <iostream>
#include <vector>
#include "SimpleThreadPool.hpp"

SimpleThreadPool::SimpleThreadPool(std::size_t numThreads) : m_numThreads(numThreads), m_nextContext(0) {
  for (std::size_t i = 0; i < m_numThreads; i++) {
    ctxptr ctx(new boost::asio::io_context);
    m_ctxs.push_back(ctx);
    m_works.push_back(boost::asio::make_work_guard(*ctx));

    std::thread th = std::thread([ctx]() {
      try {
        ctx->run();
      } catch (const std::exception& e) {
        std::cerr << "Exception in thread: " << e.what() << std::endl;
      }
    });
    struct sched_param param;
    param.sched_priority = 0;
    if (0 != pthread_setschedparam(th.native_handle(), SCHED_OTHER, &param)) {
      std::cerr << "Failed to set thread priority" << std::endl;
    }
    th.detach();
  }
}
void SimpleThreadPool::Stop() {
  for (int i = 0; i < m_numThreads; i++) {
    m_ctxs[i]->stop();
  }
}

boost::asio::io_context& SimpleThreadPool::GetContext() {
  boost::asio::io_context& ctx = *m_ctxs[m_nextContext];
  m_nextContext++;
  if (m_nextContext >= m_numThreads) {
    m_nextContext = 0;
  }
  return ctx;
}

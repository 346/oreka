#ifndef __SIMPLETHREADPOOL_H__
#define __SIMPLETHREADPOOL_H__ 1

#include <boost/asio.hpp>
#include <thread>
#include <iostream>
#include <vector>
#include <list>
#include <memory>

class SimpleThreadPool {
	public:
		explicit SimpleThreadPool();
    void Run(std::size_t numThreads);
    void Stop();

		boost::asio::io_context& GetContext();

	private:
    SimpleThreadPool(const SimpleThreadPool&) = delete;
    SimpleThreadPool& operator=(const SimpleThreadPool&) = delete;
    typedef std::shared_ptr<boost::asio::io_context> ctxptr;
    typedef boost::asio::executor_work_guard<boost::asio::io_context::executor_type> workguard;


		std::vector<ctxptr> m_ctxs;
    std::list<workguard> m_works;
		std::size_t m_numThreads;
		std::size_t m_nextContext;
};


#endif

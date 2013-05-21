#include <ilias/mq_ptr.h>
#include <ilias/promise.h>
#include <ilias/workq.h>
#include <ilias/wq_callback.h>
#include <ilias/threadpool.h>
#include <iostream>
#include <memory>
#include <utility>

using namespace ilias;
using namespace std;

const int min_prime = 2;
const int max_prime = 1000001;


class prime_reader
:	public std::enable_shared_from_this<prime_reader>
{
private:
	ilias::workq_ptr wq;
	ilias::promise<void> prom;

	static void
	filter(unsigned int test,
	    ilias::mq_out_ptr<unsigned int>& source,
	    ilias::mq_in_ptr<unsigned int>& drain)
	{
		source.dequeue([&test, &drain](unsigned int v) {
			if (v % test != 0U)
				drain.enqueue(v);
		    });
	}

	void
	install_filter(unsigned int v, ilias::mq_out_ptr<unsigned int> source,
	    ilias::mq_in_ptr<unsigned int> drain)
	{
		using namespace std::placeholders;

		callback(source, this->wq,
		    std::bind(&prime_reader::filter, v, _1, std::move(drain)),
		    workq_job::TYPE_PARALLEL | workq_job::TYPE_PERSIST);
	}

	void
	callback_fn(ilias::mq_out_ptr<unsigned int>& tail)
	{
		auto self = this->shared_from_this();

		/* Read one value only each time. */
		tail.dequeue([&](unsigned int v) {
			/* Print new discovered value. */
			std::cout << v << std::endl;

			/*
			 * Create new position for numbers not a multiple of v.
			 */
			auto new_tail = ilias::new_mq_ptr<unsigned int>();

			/*
			 * Install filtering callback between current tail
			 * and new tail.
			 */
			install_filter(v, tail, new_tail);

			this->install_callback(std::move(new_tail),
			    self);
		    }, 1);
	}

	void
	install_callback(ilias::mq_out_ptr<unsigned int> tail,
	    std::shared_ptr<prime_reader> self)
	{
		using namespace std::placeholders;

		callback(tail, wq,
		    std::bind(&prime_reader::callback_fn,
		      std::move(self), _1));
	}

	prime_reader(ilias::workq_service_ptr wqs)
	:	wq(wqs->new_workq()),
		prom(ilias::new_promise<void>())
	{
		/* Empty body. */
	}

public:
	~prime_reader() noexcept
	{
		this->prom.set();
	}

	static std::tuple<ilias::mq_in_ptr<unsigned int>, ilias::future<void>>
	create_sieve(workq_service_ptr wqs)
	{
		std::shared_ptr<prime_reader> pr{ new prime_reader{ std::move(wqs) }};
		auto rv = ilias::new_mq_ptr<unsigned int>();
		pr->install_callback(rv, pr);
		return std::make_tuple(std::move(rv), pr->prom);
	}
};


int
main()
{
	ilias::threadpool tp;
	future<void> completed;
	{
		auto wqs = ilias::new_workq_service();
		ilias::threadpool_attach(*wqs, tp);

		ilias::mq_in_ptr<unsigned int> drain;
		std::tie(drain, completed) = prime_reader::create_sieve(wqs);
		wqs->new_workq()->once(
		    std::bind([](ilias::mq_in_ptr<unsigned int>& drain) {
			for (auto i = min_prime; i != max_prime; ++i)
				drain.enqueue(i);
		    }, std::move(drain)));
	}

	completed.get();
	return 0;
}

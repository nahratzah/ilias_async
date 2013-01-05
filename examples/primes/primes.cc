#include <ilias/msg_queue.h>
#include <ilias/workq.h>
#include "val_iter.h"
#include <iostream>
#include <memory>

using namespace ilias;
using namespace std;

const int min_prime = 2;
const int max_prime = 1000001;


/*
 * Prime reader is the tail-end of the message queue fabric.
 * Each time it reads a value, it prints this value and inserts a message queue
 * that will filter this new value.
 */
class prime_reader
{
public:
	typedef msg_queue<int>::out_refpointer mq_pointer;

private:
	mq_pointer m_input;
	workq_service_ptr m_wqs;

public:
	prime_reader(workq_service_ptr wqs, mq_pointer in) noexcept
	:	m_input(in),
		m_wqs(wqs)
	{
		m_input->assign_pop_ev(std::bind(&prime_reader::handle_prime,
		    this));
	}

	~prime_reader() noexcept
	{
		m_input->clear_pop_ev();
	}

private:
	static msg_queue_detail::msgq_opt_data<int>
	prime_filter(int my_prime, int v) noexcept
	{
		msg_queue_detail::msgq_opt_data<int> rv;
		if (v % my_prime != 0)
			rv = msg_queue_detail::msgq_opt_data<int>(v);
		return rv;
	}

	static void
	new_prime_filter(workq_service_ptr wqs,
	    msg_queue<int>::in_refpointer mq_in,
	    msg_queue<int>::out_refpointer mq_out,
	    int prime)
	{
		using namespace std::placeholders;

		mqtf_transform(wqs->new_workq(), mq_in, mq_out,
		    std::bind(&prime_filter, prime, _1),
		    workq_job::TYPE_PARALLEL);
	}

	void
	inject_mq(int prime)
	{
		auto mq = make_msg_queue<int>();
		m_input->clear_pop_ev();
		new_prime_filter(m_wqs, m_input, mq.first, prime);
		m_input = mq.second;
		m_input->assign_pop_ev(std::bind(&prime_reader::handle_prime,
		    this));
	}

	void
	handle_prime() noexcept
	{
		if (!m_input)
			return;

		msg_queue_detail::msgq_opt_data<int> v;
		while ((v = m_input->pop())) {
			cout << *v << endl;
			try {
				inject_mq(*v);
			} catch (...) {
				cout << "Exception during "
				    "message queue injection." << endl;
				m_input.reset();
				return;
			}
		}

		if (!m_input->has_input_conn())
			m_input.reset();
	}
};

int
main()
{
	auto wqs = new_workq_service();
	unique_ptr<prime_reader> pr;

	{
		auto mq = make_msg_queue<int>();

		/* Bind a value generator to the input side. */
		make_msgq_generator(wqs->new_workq(), mq.first,
		    val_iter<int>(min_prime), val_iter<int>(max_prime));
		mq.first = nullptr;

		/* Bind prime reader to output side. */
		pr = unique_ptr<prime_reader>(
		    new prime_reader(wqs, mq.second));	/* XXX wrong pointer type. */
		mq.second = nullptr;
	}
}

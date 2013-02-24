#include <ilias/mq/model/msg_queue.h>

namespace ilias {
namespace mq_model {


namespace {

/* Clone a pointer value, will slice. */
template<typename Ptr>
Ptr
clone(const Ptr& p)
{
	typedef typename Ptr::element_type impl_type;
	return (p ? new impl_type(*p) : nullptr);
}

} /* namespace ilias::mq_model::<unnamed> */


msg_queue::~msg_queue() noexcept
{
	return;
}

std::string
msg_queue::get_typename() const
{
	std::ostringstream rv;
	rv << "ilias::msg_queue<" << this->m_type << ">";
	return rv.str();
}


transformer::~transformer() noexcept
{
	return;
}

std::string
transformer::get_typename() const
{
	return this->m_type;
}


}} /* namespace ilias::mq_model */

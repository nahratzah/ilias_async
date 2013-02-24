#include <string>
#include <vector>
#include <ilias/mq/model/model_cpp.h>

namespace ilias {
namespace mq_model {


/* A simple message queue defintion. */
class msg_queue
{
public:
	/* Message type. */
	cpp_name m_type;
	/* Optional allocator type. */
	std::shared_ptr<cpp_name> m_alloc;	/* XXX use optional type. */
	/* Includes for message type. */
	std::vector<cpp_include> m_includes;

	msg_queue() = default;
	~msg_queue() noexcept;
	msg_queue(const msg_queue&) = default;
	msg_queue(msg_queue&&) = default;
	msg_queue& operator=(const msg_queue&) = default;
	msg_queue& operator=(msg_queue&&) = default;

	/* Returns the typename definition. */
	std::string get_typename() const;

	friend void
	swap(msg_queue& lhs, msg_queue& rhs) noexcept
	{
		using std::swap;

		swap(lhs.m_type, rhs.m_type);
		swap(lhs.m_alloc, rhs.m_alloc);
		swap(lhs.m_includes, rhs.m_includes);
	}
};

/* Petrinet transformation declaration. */
class transformer
{
public:
	/* Type name of the transformer. */
	cpp_name m_type;
	/* Includes required for transformer inclusion. */
	std::vector<cpp_include> m_includes;
	/* Transformation input queues (name, type tuple). */
	std::map<std::string, cpp_name> m_input;
	/* Transformation output queues (name, type tuple). */
	std::map<std::string, cpp_name> m_output;

	/* Returns the typename definition of this transformer. */
	std::string get_typename() const;
};

class petrinet
{
public:
	std::map<std::string, transformer> m_transitions;
	std::map<std::string, msg_queue> m_places;
	std::set<std::pair<std::string, std::string>> m_edges;
};


}} /* namespace ilias::mq_model */

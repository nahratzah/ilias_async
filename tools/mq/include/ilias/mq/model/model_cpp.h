#include <string>

namespace ilias {
namespace mq_model {


/* Include definition for C++ file. */
class cpp_include
{
public:
	/*
	 * Include locality.
	 *
	 * GLOBAL: use #include <...>
	 * LOCAL: use #include "..."
	 */
	enum locality
	{
		GLOBAL,
		LOCAL
	};

	/* Include file locality. */
	locality m_locality;
	/* Include file name. */
	std::string m_file;

	cpp_include() = default;
	cpp_include(const cpp_include&) = default;
	cpp_include(cpp_include&&) = default;
	cpp_include& operator=(const cpp_include&) = default;
	cpp_include& operator=(cpp_include&&) = default;

	bool operator==(const cpp_include&) const = default;

	bool
	operator<(const cpp_include& o) const noexcept
	{
		if (this->m_locality != o.m_locality)
			return (this->m_locality < o.m_locality);
		return (this->m_file < o.m_file);
	}
};

/*
 * Name of a C++ class.
 *
 * XXX maybe this needs to be parsed?
 */
using cpp_name = std::string;


}} /* namespace ilias::mq_model */

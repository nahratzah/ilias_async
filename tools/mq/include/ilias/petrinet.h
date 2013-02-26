namespace ilias {


class petrinet
{
private:
	std::map<std::string, place> m_places;
	std::map<std::string, transition> m_transitions;
	std::map<std::string, std::string> m_edges;
	std::set<std::string> m_inputs;
	std::set<std::string> m_outputs;

public:
	enum class transition_validation
	:	std::uint8_t
	{
		NONE = 0x0,
		NO_INPUT = 0x01,	/* Transition is missing source. */
		NO_OUTPUT = 0x02,	/* Transition is missing source. */
	};

	enum class edge_validation
	:	std::uint8_t
	{
		NONE = 0x0,
		TRANS_TO_TRANS = 0x01,	/* Edge connects 2 transitions. */
		PLACE_TO_PLACE = 0x02,	/* Edge connects 2 places. */
		SOURCE_MISSING = 0x04,	/* Edge input is not connected. */
		DEST_MISSING = 0x08,	/* Edge output is not connected. */
		CONNECTED_INPUT = 0x10,	/* Input has connections. */
		CONNECTED_OUTPUT = 0x20,/* Output has connections. */
		OUT_IS_INPUT = 0x40,	/* Output side is marked as input. */
		IN_IS_OUTPUT = 0x80,	/* Input side is marked as output. */
	};

private:
	edge_validation
	validate_edge(const std::pair<std::string, std::string>& edge) const
	    noexcept
	{
		template<typename Haystack, typename Needle>
		bool
		has_element(const Haystack& haystack, const Needle& needle)
		    noexcept
		{
			using std::end;

			return haystack.find(needle) != haystack.end();
		}

		edge_validation result = edge_validation::NONE;
		const std::string& e0 = std::get<0>(edge);
		const std::string& e1 = std::get<1>(edge);

		const bool e0_is_trans = has_element(this->m_transitions, e0);
		const bool e1_is_trans = has_element(this->m_transitions, e1);
		const bool e0_is_place = has_element(this->m_places, e0);
		const bool e1_is_place = has_element(this->m_places, e1);
		const bool e0_is_input = has_element(this->m_inputs, e0);
		const bool e1_is_input = has_element(this->m_inputs, e1);
		const bool e0_is_output = has_element(this->m_outputs, e0);
		const bool e1_is_output = has_element(this->m_outputs, e1);

		if (e0_is_trans && e1_is_trans)
			result |= edge_validation::TRANS_TO_TRANS;
		if (e0_is_place && e1_is_place)
			result |= edge_validation::PLACE_TO_PLACE;
		if (!e0_is_trans && !e0_is_place && !e0_is_input)
			result |= edge_validation::SOURCE_MISSING;
		if (!e1_is_trans && !e1_is_place && !e1_is_output)
			result |= edge_validation::DEST_MISSING;
		if (e0_is_input && (e0_is_trans || e0_is_place))
			result |= edge_validation::CONNECTED_INPUT;
		if (e1_is_output && (e1_is_trans || e1_is_place))
			result |= edge_validation::CONNECTED_OUTPUT;
		if (e1_is_input)
			result |= edge_validation::OUT_IS_INPUT;
		if (e0_is_output)
			result |= edge_validation::IN_IS_OUTPUT;

		return result;
	}

	transition_validation
	validate_transition(const std::pair<std::string, transition>& t) const
	{
		transition_validation result = transition_validation::NONE;

		unsigned int n_input = 0, n_output = 0;
		std::for_each(begin(this->m_edges), end(this->m_edges),
		    [&](const std::pair<std::string, std::string>& i) {
			if (std::get<1>(i) == std::get<0>(t))
				++this->n_input;
			if (std::get<0>(i) == std::get<0>(t))
				++this->n_output;
		    });

		if (n_input == 0)
			result |= transition_validation::NO_INPUT;
		if (n_output == 0)
			result |= transition_validation::NO_OUTPUT;
		return result;
	}

public:
	std::map<std::pair<std::string, std::string>, edge_validation>
	validate_edges() const noexcept
	{
		std::map<std::pair<std::string, std::string>, edge_validation>
		    rv;

		for (const auto& i : this->m_edges) {
			const auto ev = validate_edge(i);
			if (ev != edge_validation::NONE)
				rv[i] = ev;
		}
		return rv;
	}
};

inline petrinet::edge_validation
operator| (const petrinet::edge_validation& lhs,
    const petrinet::edge_validation& rhs) noexcept
{
	return static_cast<petrinet::edge_validation>(int(lhs) | int(rhs));
}

inline petrinet::edge_validation
operator& (const petrinet::edge_validation& lhs,
    const petrinet::edge_validation& rhs) noexcept
{
	return static_cast<petrinet::edge_validation>(int(lhs) | int(rhs));
}

inline petrinet::edge_validation&
operator|= (const petrinet::edge_validation& lhs,
    const petrinet::edge_validation& rhs) noexcept
{
	return (lhs = lhs | rhs);
}

inline petrinet::edge_validation&
operator&= (const petrinet::edge_validation& lhs,
    const petrinet::edge_validation& rhs) noexcept
{
	return (lhs = lhs & rhs);
}


} /* namespace ilias */

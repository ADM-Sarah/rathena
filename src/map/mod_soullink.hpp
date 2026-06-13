#ifndef MOD_SOULLINK_HPP
#define MOD_SOULLINK_HPP
#include "../common/database.hpp"
#include "script.hpp"

namespace mods {
struct soul_link_data {
	uint16 soul_link;
	script_code* script = nullptr;
	soul_link_data(uint16 soul_link) :soul_link{ soul_link } {}
	~soul_link_data();
};

class SoulLinkDabatase : public TypesafeYamlDatabase<uint16, soul_link_data> {
public:
	SoulLinkDabatase() : TypesafeYamlDatabase("SOUL_LINK_DB", 1) {

	}
	const std::string getDefaultLocation() override;
	uint64 parseBodyNode(const ryml::NodeRef& node) override;
};
extern SoulLinkDabatase soul_link_db;
}

#endif // !MOD_SOULLINK_HPP
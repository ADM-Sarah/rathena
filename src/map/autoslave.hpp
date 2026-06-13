#ifndef AUTOSLAVE_HPP
#define AUTOSLAVE_HPP

#include <common/cbasetypes.hpp>
#include <common/timer.hpp>

namespace autoslave {

TIMER_FUNC(unit_autoslave_timer);
void register_timers();

}
#endif
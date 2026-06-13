#include "autoslave.hpp"

#include <cstdlib>
#include <cstring>

#include <common/random.hpp>
#include <common/showmsg.hpp>
#include <common/utils.hpp>
#include <common/socket.hpp>

#include "battle.hpp"
#include "clif.hpp"
#include "guild.hpp"
#include "map.hpp"
#include "party.hpp"
#include "path.hpp"
#include "pc.hpp"
#include "skill.hpp"
#include "status.hpp"

namespace autoslave {

static int32 FLeaderID;
static int32 FLeaderRange;
static block_list* FLeader_BL;

static inline bool a_partymember(const party_data* party, const map_session_data* sd) {
    if (!party || !sd) return false;
    for (int32 i = 0; i < MAX_PARTY; i++)
        if (party->party.member[i].char_id == sd->status.char_id)
            return true;
    return false;
}

static inline void fleader_reset() {
    FLeaderRange = 999;
    FLeaderID  = -1;
}

enum SupportMode : uint8 {
    MODE_HEAL, MODE_INCAGI, MODE_BLESS, MODE_ANGELUS, MODE_MAGNIFICAT,
    MODE_GLORIA, MODE_ASSUMPTIO, MODE_KYRIE, MODE_RESU, MODE_MANUS, MODE_SUFFRAGIUM,
};

struct SupportTargetCtx {
    party_data*       p;
    map_session_data* caster;
    SupportMode       mode;
};

static int fleader_support (block_list* bl, va_list ap) {
    auto* ctx = va_arg(ap, SupportTargetCtx*);
    auto* sd  = (map_session_data*)bl;
    if (!sd) return 0;

    auto lacks = [&](sc_type t)->bool { return !sd->sc.getSCE(t); };

    switch (ctx->mode) {
    case MODE_HEAL: {
        if (pc_isdead(sd)) return 0;
        if (!a_partymember(ctx->p, sd)) return 0;
        const int32 hp  = (int32)sd->battle_status.hp;
        const int32 mhp = (int32)sd->battle_status.max_hp;
        const int32 sp  = (int32)ctx->caster->battle_status.sp;
        const int32 msp = (int32)ctx->caster->battle_status.max_sp;

        const bool lowHP   = (mhp > 0) && (hp * 100 < mhp * 55);
        const int32 casterSPpct = (msp > 0) ? (sp * 100) / msp : 0;
        const int32 targHPpct   = (mhp > 0) ? (hp * 100) / mhp : 100;
        const bool spVsHP = (casterSPpct > targHPpct + 12);
        const bool ninja95 = (mhp > 0) && (hp * 100 < mhp * 95) && (pc_checkskill(sd, NJ_ISSEN) >= 10);

        if (lowHP || spVsHP || ninja95) {
            if (FLeaderRange > targHPpct) {
                FLeaderRange = targHPpct;
                FLeader_BL = bl; FLeaderID = sd->id;
            }
            return 1;
        }
        return 0;
    }
    case MODE_RESU:
        if (pc_isdead(sd)) { FLeader_BL = bl; FLeaderID = sd->id; return 1; }
        return 0;

    default:
        if (pc_isdead(sd)) return 0;
        if (!a_partymember(ctx->p, sd)) return 0;

        switch (ctx->mode) {
        case MODE_INCAGI:     if (lacks(SC_INCREASEAGI)) { FLeader_BL = bl; FLeaderID = sd->id; return 1; } return 0;
        case MODE_BLESS:      if (lacks(SC_BLESSING) || sd->sc.getSCE(SC_CURSE)) { FLeader_BL = bl; FLeaderID = sd->id; return 1; } return 0;
        case MODE_ANGELUS:    if (lacks(SC_ANGELUS))   { FLeader_BL = bl; FLeaderID = sd->id; return 1; } return 0;
        case MODE_MAGNIFICAT: if (lacks(SC_MAGNIFICAT)){ FLeader_BL = bl; FLeaderID = sd->id; return 1; } return 0;
        case MODE_GLORIA:     if (lacks(SC_GLORIA))    { FLeader_BL = bl; FLeaderID = sd->id; return 1; } return 0;
        case MODE_ASSUMPTIO:  if (lacks(SC_ASSUMPTIO)) { FLeader_BL = bl; FLeaderID = sd->id; return 1; } return 0;
        case MODE_KYRIE:      if (lacks(SC_KYRIE) && lacks(SC_ASSUMPTIO)) { FLeader_BL = bl; FLeaderID = sd->id; return 1; } return 0;
        case MODE_MANUS:
            if (sd->aa.autoslavemode == 1) return 0;
            if (lacks(SC_IMPOSITIO) && ((sd->battle_status.batk > sd->status.base_level) || (sd->battle_status.batk > 120))) {
                FLeader_BL = bl; FLeaderID = sd->id; return 1;
            }
            return 0;
        case MODE_SUFFRAGIUM:
        #ifdef RENEWAL
            if (lacks(SC_SUFFRAGIUM) && ((sd->battle_status.int_ * 2 > sd->status.base_level) || (sd->battle_status.rhw.matk > 120))) {
                FLeader_BL = bl; FLeaderID = sd->bl.id; return 1;
            }
        #else
            if (lacks(SC_SUFFRAGIUM) && (sd->battle_status.int_ * 2 > sd->status.base_level)) {
                FLeader_BL = bl; FLeaderID = sd->id; return 1;
            }
        #endif
            return 0;
        default: return 0;
        }
    }
}

static void sitdown(map_session_data *sd) {
    pc_setsit(sd);
    skill_sit(sd, 1);
    clif_sitting(*sd);
}

static void saythis(map_session_data* src, const char* message, int32 chance_divisor)
{
    if ((rand() % chance_divisor) != 1) return;
    char msg[300];
    safesnprintf(msg, sizeof(msg), "%s:%s", src->status.name, message);
    party_send_message(src, msg, (int32)strlen(msg) + 1);
}

static bool duplicateskill(struct party_data *p, int skillID) {
    if (!p) return false;
    for (int32 i = 0; i < MAX_PARTY; i++) {
        if (auto* sd = p->data[i].sd) {
            auto* ud = unit_bl2ud(sd);
            if (ud && ud->skill_id == skillID) return true;
        }
    }
    return false;
}

static bool canskill(map_session_data *sd)
{
    return (sd->ud.skilltimer == INVALID_TIMER) && (DIFF_TICK(gettick(), sd->ud.canact_tick) >= 0);
}

static int unit_skilluse_ifable(struct block_list *src, int target_id, uint16 skill_id, uint16 skill_lv)
{
    map_session_data *sd = (map_session_data*)src;
    int32 inf = skill_get_inf(skill_id);
    t_tick tick = gettick();

    if (skill_get_sp(skill_id, skill_lv) > sd->battle_status.sp) return 0;

    if (battle_config.idletime_option & IDLE_USESKILLTOID)
        sd->idletime = last_tick;

    if ((pc_cant_act2(sd) || sd->chatID) && skill_id != RK_REFRESH && !(skill_id == SR_GENTLETOUCH_CURE &&
        (sd->sc.opt1 == OPT1_STONE || sd->sc.opt1 == OPT1_FREEZE || sd->sc.opt1 == OPT1_STUN)) &&
        sd->state.storage_flag && !(inf & INF_SELF_SKILL))
        return 0;

    if (pc_issit(sd)) return 0;
    if (skill_isNotOk(skill_id, *sd)) return 0;

    if (sd->id != target_id && (inf & INF_SELF_SKILL)) target_id = sd->id;
    if (target_id < 0 && -target_id == sd->id) target_id = sd->id;

    if (sd->ud.skilltimer != INVALID_TIMER) {
        if (skill_id != SA_CASTCANCEL && skill_id != SO_SPELLFIST) return 0;
    } else if (DIFF_TICK(tick, sd->ud.canact_tick) < 0) {
        if (sd->skillitem != skill_id) return 0;
    }

    if (sd->sc.option & OPTION_COSTUME) return 0;
    if (auto sce_basilica = sd->sc.getSCE(SC_BASILICA)) {
        if (skill_id != HP_BASILICA || sce_basilica->val4 != sd->id) return 0;
    }

    if (sd->skillitem == skill_id) {
        if (skill_lv != sd->skillitemlv) skill_lv = sd->skillitemlv;
        if (!(inf & INF_SELF_SKILL)) pc_delinvincibletimer(sd);
        unit_skilluse_id(sd, target_id, skill_id, skill_lv);
        return 0;
    }
    sd->skillitem = sd->skillitemlv = 0;

    if (SKILL_CHK_GUILD(skill_id)) {
        skill_lv = sd->state.gmaster_flag ? guild_checkskill(sd->guild->guild, skill_id) : 0;
    } else {
        skill_lv = min(pc_checkskill(sd, skill_id), skill_lv);
    }

    pc_delinvincibletimer(sd);
    return unit_skilluse_id(src, target_id, skill_id, skill_lv);
}

TIMER_FUNC(unit_autoslave_timer)
{
    block_list *bl = map_id2bl(id);
    if (!bl) return 0;

    unit_data *ud = unit_bl2ud(bl);
    if (!ud) return 0;

    auto* sd = (map_session_data*)bl;

    if (sd->aa.autoslavemode == 0 || sd->state.autoattack == 0) return 0;
    if (bl->type != BL_PC) { return 0; }
    if (status_isdead(*bl)) return 0;
    if (pc_cant_act2(sd)) return 0;

    int32 party_id = sd->status.party_id;
    block_list *leaderbl = nullptr;
    int32 leaderdistance = 999;
    map_session_data *leadersd = nullptr;
    party_data *p = party_search(party_id);

    if (p) {
        for (int32 i = 0; i < MAX_PARTY; ++i) {
            if (p->party.member[i].leader) {
                leadersd = map_charid2sd(p->party.member[i].char_id);
                break;
            }
        }
    }

    if (leadersd && leadersd != sd) {
         leaderbl = (block_list*)leadersd;
        const bool same_map = (leaderbl->m == bl->m);
        leaderdistance = same_map ? distance_bl(leaderbl, bl) : 999;
        const t_tick now = gettick();

        if (battle_config.autoslave_teleport) {
            if (same_map && leaderdistance <= 10) {
                if (!sd->aa.autoattack_initialized) {
                    sd->aa.autoattack_initialized = true;
                    saythis(sd, " Master located. Auto teleport enabled.", 5);
                }
                sd->aa.master_lost_tick = 0;
            }

            if (!same_map) {
                if (!sd->aa.autoattack_initialized) {
                    if (DIFF_TICK(now, sd->aa.cant_teleport_tick) >= 5000) {
                        saythis(sd, " We must meet first before auto teleport can activate.", 5);
                        sd->aa.cant_teleport_tick = now;
                    }
                    return 0;
                }
                if (sd->aa.master_lost_tick == 0) sd->aa.master_lost_tick = now;

                if (DIFF_TICK(now, sd->aa.cant_teleport_tick) >= 5000) {
                    saythis(sd, " I can't teleport to a different map where my master is.", 5);
                    sd->aa.cant_teleport_tick = now;
                }

                if (DIFF_TICK(now, sd->aa.master_lost_tick) >= 30000) {
                    saythis(sd, " I lost my master. Stopping autoattack.", 5);
                    sd->aa.autoattack_initialized = false;
                    sd->aa.master_lost_tick = 0;
                    sd->aa.cant_teleport_tick = now;
                    autoattack_end(sd);
                }
                return 0;
            }

            if (!sd->aa.autoattack_initialized) {
                if (DIFF_TICK(now, sd->aa.cant_teleport_tick) >= 5000) {
                    saythis(sd, " We must meet first before auto teleport can activate.", 5);
                    sd->aa.cant_teleport_tick = now;
                }
                return 0;
            }

            if (leaderdistance > 20 && DIFF_TICK(now, sd->aa.last_teleport_tick) >= 2000) {

                if (sd->state.autoattack && (sd->state.autotrade || sd->fd == 0)) {
                    if (DIFF_TICK(now, sd->aa.cant_teleport_tick) >= 5000) {
                        char msg[256];
                        safesnprintf(msg, sizeof(msg),
                            "%s:Teleport not working on battle offline.",
                            sd->status.name
                        );
                        party_send_message(sd, msg, (int32)strlen(msg) + 1);
                        sd->aa.cant_teleport_tick = now;
                    }
                    return 0;
                }

                if (map_getmapdata(leadersd->m)->getMapFlag(MF_NOTELEPORT)) {
                    saythis(sd, " Can't teleport to master's map. Map is no-teleport.", 5);
                    return 0;
                }
                auto* md = map_getmapdata(leaderbl->m);
                int16 tx = cap_value(leaderbl->x + (rnd() % 3) - 1, 0, md->xs - 1);
                int16 ty = cap_value(leaderbl->y + (rnd() % 3) - 1, 0, md->ys - 1);

                int32 lv = pc_checkskill(sd, AL_TELEPORT);
                int32 sp = skill_get_sp(AL_TELEPORT, lv);

                if (lv > 0 && sd->status.sp >= sp) {
                    skill_consume_requirement(sd, AL_TELEPORT, lv, 2);
                    status_heal(sd, 0, -sp, 1);
                    if (pc_setpos(sd, leadersd->mapindex, tx, ty, CLR_TELEPORT) == 0) {
                        sd->aa.last_teleport_tick = now;
                        saythis(sd, " Teleporting to master!", 5);
                    } else {
                        saythis(sd, " Teleport failed (bad cell).", 5);
                    }
                    return 0;
                }
            }
        }
        if (same_map) {
            if (!battle_config.autoslave_teleport) {
                sd->aa.autoattack_initialized = true;
                sd->aa.master_lost_tick = 0;
            }

            if (leaderdistance > 2 && leaderdistance <= 20) {
                unit_walktobl(bl, leaderbl, 2, 1);
            }
        }
    }

    if (leadersd && !pc_issit(leadersd)) {
        if (pc_issit(sd) && pc_setstand(sd, false)) {
            skill_sit(sd, 0);
            clif_standing(*sd);
        }
    }

    if (pc_issit(sd)) return 0;

    if (sd->battle_status.sp < (sd->battle_status.max_sp / 10))
        saythis(sd, " Please give me a moment to rest. I need SP to continue supporting!", 50);

    if (sd->aa.autoresu == 1) {
        if (pc_checkskill(sd, ALL_RESURRECTION) > 0 && pc_inventory_count(sd, ITEMID_BLUE_GEMSTONE) < 8)
            saythis(sd, " I'm low on Blue Gemstones!", 600);
        if (canskill(sd) && pc_checkskill(sd, ALL_RESURRECTION) > 0) {
            fleader_reset();
            SupportTargetCtx ctx{ p, sd, MODE_RESU };
            map_foreachinrange(fleader_support, sd, 9, BL_PC, &ctx);
            if (FLeaderID > -1) {
                if (pc_search_inventory(sd, ITEMID_BLUE_GEMSTONE) >= 0)
                    unit_skilluse_ifable(sd, FLeaderID, ALL_RESURRECTION, pc_checkskill(sd, ALL_RESURRECTION));
                else
                    saythis(sd, " I'm out of Blue Gemstones!", 5);
            }
        }
    }

    auto run_mode = [&](SupportMode mode, uint16 skill, bool self, bool check_dup)->void {
        if (!canskill(sd)) return;
        if (pc_checkskill(sd, skill) <= 0) return;
        fleader_reset();
        SupportTargetCtx ctx{ p, sd, mode };
        map_foreachinrange(fleader_support, sd, 9, BL_PC, &ctx);
        if (FLeaderID <= -1) return;
        if (check_dup && duplicateskill(p, skill)) return;
        if (self) unit_skilluse_ifable(sd, SELF, skill, pc_checkskill(sd, skill));
        else      unit_skilluse_ifable(sd, FLeaderID, skill, pc_checkskill(sd, skill));
    };

    if (sd->aa.autohealing == 1) run_mode(MODE_HEAL,       AL_HEAL,       false, false);
    if (sd->aa.automagni  == 1) run_mode(MODE_MAGNIFICAT,  PR_MAGNIFICAT, true,  true);
    if (sd->aa.autoangelus== 1) run_mode(MODE_ANGELUS,     AL_ANGELUS,    true,  false);
    if (sd->aa.autoagi    == 1) run_mode(MODE_INCAGI,      AL_INCAGI,     false, true);
    if (sd->aa.autobless  == 1) run_mode(MODE_BLESS,       AL_BLESSING,   false, false);
    if (sd->aa.autoassump == 1) run_mode(MODE_ASSUMPTIO,   HP_ASSUMPTIO,  false, true);
    if (sd->aa.autokyrie  == 1) run_mode(MODE_KYRIE,       PR_KYRIE,      false, true);
    if (sd->aa.autogloria == 1) run_mode(MODE_GLORIA,      PR_GLORIA,     true,  false);
    if (sd->aa.autoimpo   == 1) run_mode(MODE_MANUS,       PR_IMPOSITIO,  false, true);
    if (sd->aa.autosuffra == 1) run_mode(MODE_SUFFRAGIUM,  PR_SUFFRAGIUM, false, true);

    if (leadersd && pc_issit(leadersd) && !pc_issit(sd)) { sitdown(sd); return 0; }

    return 0;
}

void register_timers() {
    add_timer_func_list(unit_autoslave_timer, "unit_autoslave_timer");
}

}
// ownership_logic.hpp - who provides which service at each airfield, worked out the way the UK Controller Plugin
// does it (https://github.com/VATSIM-UK/uk-controller-plugin, GPL-3.0):
//   src/plugin/ownership   ServiceType, ServiceProvision, AirfieldServiceProviderCollection, AirfieldOwnershipManager
//                          (GetServiceProvidersAtAirfield), AirfieldOwnershipHandler (ProcessAffectedAirfields)
//   src/plugin/controller  ActiveCallsignCollection (lead callsign per position), ActiveCallsignMonitor (a controller
//                          is active with a primary frequency), ControllerPosition (Provides*Services, HasTopdownAirfield)
// One top-down order per airfield from Core, one parser, one copy: squawks ask DeliveryControlProvidedByUser, the
// QNH monitor asks UserHasTopdownAirfield (UKCP PressureMonitor), notifications ask where the user provides a service.
// Differences from UKCP, because Core has no list of positions with frequencies:
//   * A top-down position (OKBK_TWR) is worked by any callsign with the same prefix and suffix (OKBK_TWR, OKBK_1_TWR,
//     OKBK_N_TWR), where UKCP matches facility, type and frequency.
//   * An airfield Core has no order for is covered by its own DEL, GND, TWR and APP, in that order.
// Pure logic (no Windows / EuroScope dependencies), unit-tested in test/test_logic.cpp.
// License: GPL-3.0-or-later
#pragma once

#include "kwiq_logic.hpp"

namespace kwiq::ownership {

// ---- services (UKCP ServiceType) ---------------------------------------------------------------
enum class ServiceType : unsigned { Delivery = 1, Ground = 2, Tower = 4, FinalApproach = 8, Approach = 16 };
inline ServiceType operator|(ServiceType a, ServiceType b) { return (ServiceType)((unsigned)a | (unsigned)b); }
inline ServiceType operator&(ServiceType a, ServiceType b) { return (ServiceType)((unsigned)a & (unsigned)b); }
constexpr ServiceType kAnyService = (ServiceType)31u;

// ---- top-down orders --------------------------------------------------------------------------
// Core /api/plugin/topdown: TOPDOWN|<airport>|<position>,<position>,...  lowest first,
// e.g. TOPDOWN|OKBK|OKBK_DEL,OKBK_GND,OKBK_TWR,OKBK_APP,OKAC_CTR
using TopDownOrders = std::map<std::string, std::vector<std::string>>;

inline TopDownOrders ParseTopDown(const std::string& body) {
    TopDownOrders td;
    for (const auto& line : SplitChar(body, '\n')) {
        auto f = SplitChar(Trim(line), '|');
        if (f.size() < 3 || Trim(f[0]) != "TOPDOWN") continue;
        std::string apt = Upper(Trim(f[1]));
        if (!IsAirportCode(apt)) continue;
        std::vector<std::string> order;
        for (const auto& p : SplitChar(f[2], ',')) {
            std::string pos = Upper(Trim(p));
            if (pos.find('_') != std::string::npos && std::find(order.begin(), order.end(), pos) == order.end()) order.push_back(pos);
        }
        if (!order.empty()) td[apt] = order;
    }
    return td;
}
// An airfield Core has no order for: its own positions only
inline std::vector<std::string> TopDownOrder(const TopDownOrders& td, const std::string& airport) {
    auto it = td.find(airport);
    if (it != td.end()) return it->second;
    if (!IsAirportCode(airport)) return {};
    return { airport + "_DEL", airport + "_GND", airport + "_TWR", airport + "_APP" };
}

// ---- positions (UKCP ControllerPosition) --------------------------------------------------------
// OKBK_N_TWR -> OKBK and TWR
inline std::string CallsignPrefix(const std::string& cs) { return Upper(cs.substr(0, cs.find('_'))); }
inline std::string CallsignSuffix(const std::string& cs) {
    size_t i = cs.rfind('_');
    return i == std::string::npos ? std::string() : Upper(cs.substr(i + 1));
}
// A top-down position (OKBK_TWR) is worked by OKBK_TWR, OKBK_1_TWR, OKBK_N_TWR: same prefix and suffix
inline bool PositionMatches(const std::string& position, const std::string& callsign) {
    if (position.find('_') == std::string::npos || callsign.find('_') == std::string::npos) return false;
    return CallsignPrefix(position) == CallsignPrefix(callsign) && CallsignSuffix(position) == CallsignSuffix(callsign);
}
inline bool IsGround(const std::string& p) { return CallsignSuffix(p) == "GND"; }
inline bool IsTower(const std::string& p) { return CallsignSuffix(p) == "TWR"; }
inline bool IsApproach(const std::string& p) { return CallsignSuffix(p) == "APP"; }
inline bool IsEnroute(const std::string& p) { return CallsignSuffix(p) == "CTR"; }
inline bool ProvidesGroundServices(const std::string& p) { return IsGround(p) || IsTower(p) || IsApproach(p) || IsEnroute(p); }
inline bool ProvidesTowerServices(const std::string& p) { return IsTower(p) || IsApproach(p) || IsEnroute(p); }
inline bool ProvidesApproachServices(const std::string& p) { return IsApproach(p) || IsEnroute(p); }

// ---- controllers online (UKCP ActiveCallsign, ActiveCallsignCollection) ---------------------------
struct ActiveCallsign {
    std::string callsign;
    bool isUser = false;
    bool operator==(const ActiveCallsign& o) const { return callsign == o.callsign && isUser == o.isUser; }
};
// UKCP ActiveCallsignMonitor: a controller counts once they have a primary frequency (EuroScope says 199.998 when
// they have none)
inline bool HasActiveFrequency(double primaryMHz) { return primaryMHz > 0.0 && primaryMHz < 190.0; }

class ActiveCallsigns {
public:
    // True when it changed anything
    bool Add(const std::string& callsign, bool isUser) {
        std::string cs = Upper(Trim(callsign));
        if (cs.empty()) return false;
        ActiveCallsign a{ cs, isUser };
        auto it = byCallsign_.find(cs);
        if (it != byCallsign_.end() && it->second == a) return false;
        byCallsign_[cs] = a;
        return true;
    }
    bool Remove(const std::string& callsign) { return byCallsign_.erase(Upper(Trim(callsign))) > 0; }
    void Flush() { byCallsign_.clear(); }
    bool CallsignActive(const std::string& callsign) const { return byCallsign_.count(Upper(Trim(callsign))) > 0; }
    // UKCP GetLeadCallsignForPosition: of the callsigns on a position, the first in callsign order
    const ActiveCallsign* LeadFor(const std::string& position) const {
        for (const auto& kv : byCallsign_) if (PositionMatches(position, kv.first)) return &kv.second;
        return nullptr;
    }
    bool PositionActive(const std::string& position) const { return LeadFor(position) != nullptr; }
    const ActiveCallsign* User() const {
        for (const auto& kv : byCallsign_) if (kv.second.isUser) return &kv.second;
        return nullptr;
    }
    const std::map<std::string, ActiveCallsign>& All() const { return byCallsign_; }

private:
    std::map<std::string, ActiveCallsign> byCallsign_;   // sorted by callsign, as UKCP's sets of callsigns
};

// ---- who provides what (UKCP ServiceProvision, AirfieldOwnershipManager::GetServiceProvidersAtAirfield) ----------
struct ServiceProvision {
    ServiceType service = ServiceType::Delivery;
    ActiveCallsign controller;
};

// The positions of an airfield's order that someone is on, lowest first (UKCP
// GetActiveControllersInAirfieldTopdownOrder), and what each of them provides:
//   - The first position online provides Delivery.
//   - Every GND provides Ground; failing a GND, the first position that can (TWR, APP, CTR).
//   - Every TWR provides Tower; failing a TWR, the first APP or CTR.
//   - The first APP or CTR provides Final Approach.
//   - Every APP provides Approach; failing an APP, the first CTR.
inline std::vector<ServiceProvision> ServiceProvidersAt(const std::vector<std::string>& order, const ActiveCallsigns& active) {
    std::vector<ServiceProvision> out;
    auto has = [&out](ServiceType t) {
        return std::any_of(out.begin(), out.end(), [t](const ServiceProvision& p) { return p.service == t; });
    };
    for (const auto& position : order) {
        const ActiveCallsign* lead = active.LeadFor(position);
        if (!lead) continue;
        if (out.empty()) out.push_back({ ServiceType::Delivery, *lead });
        if (!ProvidesGroundServices(position)) continue;
        if (IsGround(position) || !has(ServiceType::Ground)) out.push_back({ ServiceType::Ground, *lead });
        if (!ProvidesTowerServices(position)) continue;
        if (IsTower(position) || !has(ServiceType::Tower)) out.push_back({ ServiceType::Tower, *lead });
        if (!ProvidesApproachServices(position)) continue;
        if (!has(ServiceType::FinalApproach)) out.push_back({ ServiceType::FinalApproach, *lead });
        if (IsApproach(position) || !has(ServiceType::Approach)) out.push_back({ ServiceType::Approach, *lead });
    }
    return out;
}

// ---- the service providers at every airfield (UKCP AirfieldServiceProviderCollection) -----------------
class ServiceProviders {
public:
    void SetProvidersForAirfield(const std::string& icao, std::vector<ServiceProvision> providers) {
        if (providers.empty()) providers_.erase(icao);
        else providers_[icao] = std::move(providers);
    }
    void FlushForAirfield(const std::string& icao) { providers_.erase(icao); }
    void Flush() { providers_.clear(); }
    const std::vector<ServiceProvision>& GetServiceProviders(const std::string& icao) const {
        static const std::vector<ServiceProvision> none;
        auto it = providers_.find(icao);
        return it == providers_.end() ? none : it->second;
    }
    bool ServiceProvidedAtAirfieldByUser(const std::string& icao, ServiceType type) const {
        for (const auto& p : GetServiceProviders(icao)) if (p.service == type && p.controller.isUser) return true;
        return false;
    }
    bool DeliveryControlProvidedByUser(const std::string& icao) const { return ServiceProvidedAtAirfieldByUser(icao, ServiceType::Delivery); }
    // The callsign providing Delivery, or ""
    std::string DeliveryProviderForAirfield(const std::string& icao) const {
        for (const auto& p : GetServiceProviders(icao)) if (p.service == ServiceType::Delivery) return p.controller.callsign;
        return "";
    }
    // UKCP's bitwise check: the airfields where the user provides one of the services in `services`
    std::vector<std::string> GetAirfieldsWhereUserProvidingServices(ServiceType services) const {
        std::vector<std::string> out;
        for (const auto& kv : providers_)
            for (const auto& p : kv.second)
                if ((p.service & services) == p.service && p.controller.isUser) { out.push_back(kv.first); break; }
        return out;
    }

private:
    std::map<std::string, std::vector<ServiceProvision>> providers_;
};

// ---- keeping them up to date (UKCP AirfieldOwnershipManager, AirfieldOwnershipHandler) ------------------
// Fed with Core's top-down orders and with EuroScope's controller events; refreshes only the airfields a change
// can affect, as UKCP does.
class AirfieldOwnership {
public:
    void SetTopDown(TopDownOrders td) {
        td_ = std::move(td);
        providers_.Flush();
        for (const auto& icao : Airfields()) RefreshOwner(icao);
    }
    const TopDownOrders& TopDown() const { return td_; }

    // UKCP ActiveCallsignMonitor::ControllerUpdateEvent: active with a primary frequency, gone without one
    void ControllerUpdate(const std::string& callsign, bool isUser, bool isController, double primaryMHz) {
        bool changed = isController && HasActiveFrequency(primaryMHz) ? active_.Add(callsign, isUser) : active_.Remove(callsign);
        if (changed) ProcessAffectedAirfields(callsign);
    }
    void ControllerDisconnect(const std::string& callsign) {
        if (active_.Remove(callsign)) ProcessAffectedAirfields(callsign);
    }
    // UKCP SelfDisconnectEvent: the user logged off, so everything is forgotten
    void SelfDisconnect() { active_.Flush(); providers_.Flush(); }

    const ActiveCallsigns& Active() const { return active_; }
    const ServiceProviders& Providers() const { return providers_; }
    bool DeliveryControlProvidedByUser(const std::string& icao) const { return providers_.DeliveryControlProvidedByUser(Upper(icao)); }

    // UKCP PressureMonitor: the user's position is in the airfield's top-down order (whoever else is online)
    bool UserHasTopdownAirfield(const std::string& icao) const {
        const ActiveCallsign* me = active_.User();
        if (!me) return false;
        for (const auto& pos : TopDownOrder(td_, Upper(icao))) if (PositionMatches(pos, me->callsign)) return true;
        return false;
    }
    std::vector<std::string> UserTopdownAirfields() const {
        std::vector<std::string> out;
        for (const auto& icao : Airfields()) if (UserHasTopdownAirfield(icao)) out.push_back(icao);
        return out;
    }

private:
    // Core's airfields, and those of controllers online that Core has no order for (their own positions)
    std::vector<std::string> Airfields() const {
        std::vector<std::string> out;
        for (const auto& kv : td_) out.push_back(kv.first);
        for (const auto& kv : active_.All()) {
            std::string own = CallsignPrefix(kv.first);
            if (IsAirportCode(own) && !td_.count(own) && std::find(out.begin(), out.end(), own) == out.end()) out.push_back(own);
        }
        return out;
    }
    // UKCP RefreshOwner
    void RefreshOwner(const std::string& icao) { providers_.SetProvidersForAirfield(icao, ServiceProvidersAt(TopDownOrder(td_, icao), active_)); }
    // UKCP ProcessAffectedAirfields: every airfield whose order has a position this callsign works
    void ProcessAffectedAirfields(const std::string& callsign) {
        std::string own = CallsignPrefix(callsign);
        for (const auto& kv : td_)
            for (const auto& pos : kv.second)
                if (PositionMatches(pos, callsign)) { RefreshOwner(kv.first); break; }
        if (IsAirportCode(own) && !td_.count(own)) RefreshOwner(own);
    }

    TopDownOrders td_;
    ActiveCallsigns active_;
    ServiceProviders providers_;
};

} // namespace kwiq::ownership

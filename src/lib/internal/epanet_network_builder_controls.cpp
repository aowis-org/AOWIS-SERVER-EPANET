#include "epanet_network_builder.h"

#include "epanet_index_registry.h"
#include "epanet_network_builder_support.h"
#include "epanet_project.h"
#include "epanet_status_helpers.h"

#include <aowis/epanet/epanet_resolvers.h>

#include <QByteArray>
#include <QList>

#include <array>
#include <cmath>
#include <functional>
#include <limits>

HydraulicSimulationStatus EpanetNetworkBuilder::addControlSimple(const HydraulicControlSimple &control)
{
    int backend_type = 0;
    if (!EpanetNetworkBuilderSupport::resolveSimpleControlType(control.type, backend_type))
        return makeEpanetStatus(HydraulicSimulationStatusStage::AddControl, HydraulicSimulationStatusOperation::AddControl, HydraulicSimulationStatusEntityType::Control, control.id, control.uuid, QStringLiteral("Unsupported simple control type"));

    int link_index = 0;
    if (!resolveLinkIndex(control.link_uuid, link_index))
        return makeEpanetStatus(HydraulicSimulationStatusStage::AddControl, HydraulicSimulationStatusOperation::ResolveEntity, HydraulicSimulationStatusEntityType::Control, control.id, control.uuid, QStringLiteral("Could not resolve the link controlled by the simple control"));

    double setting = 0.0;
    switch (control.action)
    {
    case HydraulicControlActionType::Open:
        if (EpanetNetworkBuilderSupport::controlLinkSettingValueCount(control.setting) != 0)
            return makeEpanetStatus(HydraulicSimulationStatusStage::AddControl, HydraulicSimulationStatusOperation::AddControl, HydraulicSimulationStatusEntityType::Control, control.id, control.uuid, QStringLiteral("OPEN simple control action must not define a numeric setting"));
        setting = EN_SET_OPEN;
        break;
    case HydraulicControlActionType::Close:
        if (EpanetNetworkBuilderSupport::controlLinkSettingValueCount(control.setting) != 0)
            return makeEpanetStatus(HydraulicSimulationStatusStage::AddControl, HydraulicSimulationStatusOperation::AddControl, HydraulicSimulationStatusEntityType::Control, control.id, control.uuid, QStringLiteral("CLOSE simple control action must not define a numeric setting"));
        setting = EN_SET_CLOSED;
        break;
    case HydraulicControlActionType::Setting:
        if (!resolveControlLinkSetting(control.link_uuid, control.setting, setting) || setting < 0.0)
            return makeEpanetStatus(HydraulicSimulationStatusStage::AddControl, HydraulicSimulationStatusOperation::AddControl, HydraulicSimulationStatusEntityType::Control, control.id, control.uuid, QStringLiteral("Simple control setting does not match the controlled pump or valve"));
        break;
    }

    int trigger_node_index = 0;
    double trigger_value = 0.0;
    if (control.type == HydraulicControlSimpleType::LowLevel || control.type == HydraulicControlSimpleType::HighLevel)
    {
        trigger_node_index = this->indices.nodes_junctions.value(control.trigger_node_uuid, 0);
        if (trigger_node_index != 0)
        {
            if (!std::isfinite(control.trigger_pressure_head_m))
                return makeEpanetStatus(HydraulicSimulationStatusStage::AddControl, HydraulicSimulationStatusOperation::AddControl, HydraulicSimulationStatusEntityType::Control, control.id, control.uuid, QStringLiteral("Simple control trigger pressure head must be finite"));
            trigger_value = control.trigger_pressure_head_m;
        }
        else
        {
            trigger_node_index = this->indices.nodes_tanks.value(control.trigger_node_uuid, 0);
            if (trigger_node_index == 0)
                return makeEpanetStatus(HydraulicSimulationStatusStage::AddControl, HydraulicSimulationStatusOperation::ResolveEntity, HydraulicSimulationStatusEntityType::Control, control.id, control.uuid, QStringLiteral("A level control trigger must reference a junction or tank"));
            if (!std::isfinite(control.trigger_water_level_m))
                return makeEpanetStatus(HydraulicSimulationStatusStage::AddControl, HydraulicSimulationStatusOperation::AddControl, HydraulicSimulationStatusEntityType::Control, control.id, control.uuid, QStringLiteral("Simple control trigger water level must be finite"));
            trigger_value = control.trigger_water_level_m;
        }
    }
    else
    {
        const quint64 trigger_time_s = control.type == HydraulicControlSimpleType::Timer
            ? control.trigger_elapsed_time_s
            : control.trigger_time_of_day_s;
        if (trigger_time_s > static_cast<quint64>(std::numeric_limits<long>::max()))
            return makeEpanetStatus(HydraulicSimulationStatusStage::AddControl, HydraulicSimulationStatusOperation::AddControl, HydraulicSimulationStatusEntityType::Control, control.id, control.uuid, QStringLiteral("Simple control trigger time exceeds the EPANET time range"));
        if (control.type == HydraulicControlSimpleType::TimeOfDay && trigger_time_s >= 24 * 60 * 60)
            return makeEpanetStatus(HydraulicSimulationStatusStage::AddControl, HydraulicSimulationStatusOperation::AddControl, HydraulicSimulationStatusEntityType::Control, control.id, control.uuid, QStringLiteral("Time-of-day control trigger must be within one day"));
        trigger_value = static_cast<double>(trigger_time_s);
    }

    int control_index = 0;
    int error = EN_addcontrol(this->project.handle(), backend_type, link_index, setting, trigger_node_index, trigger_value, &control_index);
    if (error != 0)
    {
        HydraulicSimulationStatus status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddControl, HydraulicSimulationStatusOperation::AddControl, QStringLiteral("EN_addcontrol"), HydraulicSimulationStatusEntityType::Control, control.id, control.uuid, QStringLiteral("Failed to add simple hydraulic control"));
        if (!status.success)
        {
            status.entity.index = control_index;
            return status;
        }
    }

    error = EN_setcontrolenabled(this->project.handle(), control_index, control.enabled ? EN_TRUE : EN_FALSE);
    if (error != 0)
    {
        HydraulicSimulationStatus status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddControl, HydraulicSimulationStatusOperation::SetEntityMetadata, QStringLiteral("EN_setcontrolenabled"), HydraulicSimulationStatusEntityType::Control, control.id, control.uuid, QStringLiteral("Failed to set simple hydraulic control enabled state"));
        if (!status.success)
        {
            status.entity.index = control_index;
            return status;
        }
    }

    this->indices.controls_simple.insert(control.uuid, control_index);
    return makeEpanetSuccess();
}

HydraulicSimulationStatus EpanetNetworkBuilder::buildControlRuleText(const HydraulicControlRule &rule, QString &rule_text) const
{
    if (rule.premises.isEmpty())
        return makeEpanetStatus(HydraulicSimulationStatusStage::AddRule, HydraulicSimulationStatusOperation::AddRule, HydraulicSimulationStatusEntityType::Rule, rule.id, rule.uuid, QStringLiteral("Control rule requires at least one premise"));
    if (rule.actions_then.isEmpty())
        return makeEpanetStatus(HydraulicSimulationStatusStage::AddRule, HydraulicSimulationStatusOperation::AddRule, HydraulicSimulationStatusEntityType::Rule, rule.id, rule.uuid, QStringLiteral("Control rule requires at least one THEN action"));
    if (!std::isfinite(rule.priority))
        return makeEpanetStatus(HydraulicSimulationStatusStage::AddRule, HydraulicSimulationStatusOperation::AddRule, HydraulicSimulationStatusEntityType::Rule, rule.id, rule.uuid, QStringLiteral("Control rule priority must be finite"));

    QStringList lines;
    lines.append(QStringLiteral("RULE %1").arg(rule.id));

    for (int premise_index = 0; premise_index < rule.premises.size(); premise_index++)
    {
        const HydraulicControlRulePremise &premise = rule.premises.at(premise_index);
        if ((premise_index == 0 && premise.logical_operator != HydraulicControlRuleLogicalOperator::If)
            || (premise_index > 0 && premise.logical_operator == HydraulicControlRuleLogicalOperator::If))
        {
            return makeEpanetStatus(HydraulicSimulationStatusStage::AddRule, HydraulicSimulationStatusOperation::AddRule, HydraulicSimulationStatusEntityType::Rule, rule.id, rule.uuid, QStringLiteral("The first control-rule premise must use IF and later premises must use AND or OR"));
        }

        const QString logical_operator = EpanetNetworkBuilderSupport::ruleLogicalOperatorText(premise.logical_operator);
        const QString variable = EpanetNetworkBuilderSupport::ruleVariableText(premise.variable);
        const QString comparison = EpanetNetworkBuilderSupport::ruleComparisonText(premise.comparison);
        if (logical_operator.isEmpty() || variable.isEmpty() || comparison.isEmpty())
            return makeEpanetStatus(HydraulicSimulationStatusStage::AddRule, HydraulicSimulationStatusOperation::AddRule, HydraulicSimulationStatusEntityType::Rule, rule.id, rule.uuid, QStringLiteral("Control rule contains an unsupported premise enum value"));

        QString object_clause;
        bool variable_supported = false;
        if (premise.object == HydraulicControlRuleObject::Node)
        {
            const QString node_id = this->node_ids_by_uuid.value(premise.object_uuid);
            if (node_id.isEmpty())
                return makeEpanetStatus(HydraulicSimulationStatusStage::AddRule, HydraulicSimulationStatusOperation::ResolveEntity, HydraulicSimulationStatusEntityType::Rule, rule.id, rule.uuid, QStringLiteral("Could not resolve a node referenced by a control-rule premise"));
            object_clause = QStringLiteral("NODE %1").arg(node_id);
            variable_supported = premise.variable == HydraulicControlRuleVariable::Demand
                || premise.variable == HydraulicControlRuleVariable::Head
                || premise.variable == HydraulicControlRuleVariable::Grade
                || premise.variable == HydraulicControlRuleVariable::Level
                || premise.variable == HydraulicControlRuleVariable::Pressure
                || premise.variable == HydraulicControlRuleVariable::FillTime
                || premise.variable == HydraulicControlRuleVariable::DrainTime;
            if ((premise.variable == HydraulicControlRuleVariable::FillTime || premise.variable == HydraulicControlRuleVariable::DrainTime)
                && !this->indices.nodes_tanks.contains(premise.object_uuid))
            {
                return makeEpanetStatus(HydraulicSimulationStatusStage::AddRule, HydraulicSimulationStatusOperation::AddRule, HydraulicSimulationStatusEntityType::Rule, rule.id, rule.uuid, QStringLiteral("FILLTIME and DRAINTIME premises require a tank"));
            }
        }
        else if (premise.object == HydraulicControlRuleObject::Link)
        {
            QString link_id;
            if (!resolveLinkId(premise.object_uuid, link_id))
                return makeEpanetStatus(HydraulicSimulationStatusStage::AddRule, HydraulicSimulationStatusOperation::ResolveEntity, HydraulicSimulationStatusEntityType::Rule, rule.id, rule.uuid, QStringLiteral("Could not resolve a link referenced by a control-rule premise"));
            if (premise.variable == HydraulicControlRuleVariable::Power)
            {
                return makeEpanetStatus(
                    HydraulicSimulationStatusStage::AddRule,
                    HydraulicSimulationStatusOperation::AddRule,
                    HydraulicSimulationStatusEntityType::Rule,
                    rule.id,
                    rule.uuid,
                    QStringLiteral("Pump POWER control-rule premises are not supported by the bundled EPANET 2.3 rule engine"));
            }
            object_clause = QStringLiteral("LINK %1").arg(link_id);
            variable_supported = premise.variable == HydraulicControlRuleVariable::Flow
                || premise.variable == HydraulicControlRuleVariable::Status
                || premise.variable == HydraulicControlRuleVariable::Setting;
        }
        else if (premise.object == HydraulicControlRuleObject::System)
        {
            if (!premise.object_uuid.isNull())
                return makeEpanetStatus(HydraulicSimulationStatusStage::AddRule, HydraulicSimulationStatusOperation::AddRule, HydraulicSimulationStatusEntityType::Rule, rule.id, rule.uuid, QStringLiteral("A SYSTEM premise must not reference an entity UUID"));
            object_clause = QStringLiteral("SYSTEM");
            variable_supported = premise.variable == HydraulicControlRuleVariable::Demand
                || premise.variable == HydraulicControlRuleVariable::Time
                || premise.variable == HydraulicControlRuleVariable::ClockTime;
        }

        if (!variable_supported)
            return makeEpanetStatus(HydraulicSimulationStatusStage::AddRule, HydraulicSimulationStatusOperation::AddRule, HydraulicSimulationStatusEntityType::Rule, rule.id, rule.uuid, QStringLiteral("Control-rule variable is not valid for its object type"));

        QString value_text;
        if (premise.variable == HydraulicControlRuleVariable::Status)
        {
            if (!premise.status.has_value() || EpanetNetworkBuilderSupport::rulePremiseNumericValueCount(premise) != 0)
                return makeEpanetStatus(HydraulicSimulationStatusStage::AddRule, HydraulicSimulationStatusOperation::AddRule, HydraulicSimulationStatusEntityType::Rule, rule.id, rule.uuid, QStringLiteral("A STATUS premise requires one status value and no numeric value"));
            if (premise.comparison != HydraulicControlRuleOperator::Equal
                && premise.comparison != HydraulicControlRuleOperator::NotEqual
                && premise.comparison != HydraulicControlRuleOperator::Is
                && premise.comparison != HydraulicControlRuleOperator::IsNot)
            {
                return makeEpanetStatus(HydraulicSimulationStatusStage::AddRule, HydraulicSimulationStatusOperation::AddRule, HydraulicSimulationStatusEntityType::Rule, rule.id, rule.uuid, QStringLiteral("A STATUS premise only supports equality or inequality comparisons"));
            }
            value_text = EpanetNetworkBuilderSupport::ruleStatusText(premise.status.value());
        }
        else
        {
            double premise_value = 0.0;
            if (premise.status.has_value() || !resolveRulePremiseValue(premise, premise_value) || !std::isfinite(premise_value))
                return makeEpanetStatus(HydraulicSimulationStatusStage::AddRule, HydraulicSimulationStatusOperation::AddRule, HydraulicSimulationStatusEntityType::Rule, rule.id, rule.uuid, QStringLiteral("A numeric control-rule premise requires the quantity-specific value for its selected variable"));
            if (EpanetNetworkBuilderSupport::isTimeRuleVariable(premise.variable))
            {
                if ((premise.variable == HydraulicControlRuleVariable::Time || premise.variable == HydraulicControlRuleVariable::ClockTime)
                    && premise_value > static_cast<double>(std::numeric_limits<long>::max()))
                {
                    return makeEpanetStatus(HydraulicSimulationStatusStage::AddRule, HydraulicSimulationStatusOperation::AddRule, HydraulicSimulationStatusEntityType::Rule, rule.id, rule.uuid, QStringLiteral("Control-rule time value exceeds the EPANET time range"));
                }
                if (premise.variable == HydraulicControlRuleVariable::ClockTime && premise_value >= 24.0 * 60.0 * 60.0)
                    return makeEpanetStatus(HydraulicSimulationStatusStage::AddRule, HydraulicSimulationStatusOperation::AddRule, HydraulicSimulationStatusEntityType::Rule, rule.id, rule.uuid, QStringLiteral("CLOCKTIME premise value must be within one day"));
                value_text = QString::number(premise_value / 3600.0, 'g', 17);
            }
            else
            {
                value_text = QString::number(premise_value, 'g', 17);
            }
        }

        lines.append(QStringLiteral("%1 %2 %3 %4 %5").arg(logical_operator, object_clause, variable, comparison, value_text));
    }

    const std::function<HydraulicSimulationStatus(const QList<HydraulicControlRuleAction> &, const QString &)> append_actions = [this, &rule, &lines](const QList<HydraulicControlRuleAction> &actions, const QString &first_keyword) -> HydraulicSimulationStatus
    {
        for (int action_index = 0; action_index < actions.size(); action_index++)
        {
            const HydraulicControlRuleAction &action = actions.at(action_index);
            QString link_id;
            if (!resolveLinkId(action.link_uuid, link_id))
                return makeEpanetStatus(HydraulicSimulationStatusStage::AddRule, HydraulicSimulationStatusOperation::ResolveEntity, HydraulicSimulationStatusEntityType::Rule, rule.id, rule.uuid, QStringLiteral("Could not resolve a link referenced by a control-rule action"));
            double action_setting = 0.0;
            const int setting_value_count = EpanetNetworkBuilderSupport::controlLinkSettingValueCount(action.setting);
            const bool has_setting = resolveControlLinkSetting(action.link_uuid, action.setting, action_setting);
            if ((action.status.has_value() && setting_value_count != 0)
                || (!action.status.has_value() && (!has_setting || setting_value_count != 1)))
            {
                return makeEpanetStatus(HydraulicSimulationStatusStage::AddRule, HydraulicSimulationStatusOperation::AddRule, HydraulicSimulationStatusEntityType::Rule, rule.id, rule.uuid, QStringLiteral("A control-rule action must define exactly one status or quantity-specific setting"));
            }

            QString variable;
            QString value;
            if (action.status.has_value())
            {
                if (action.status.value() == HydraulicControlRuleStatus::Active
                    && !this->indices.links_valves.contains(action.link_uuid))
                {
                    return makeEpanetStatus(HydraulicSimulationStatusStage::AddRule, HydraulicSimulationStatusOperation::AddRule, HydraulicSimulationStatusEntityType::Rule, rule.id, rule.uuid, QStringLiteral("An ACTIVE rule action requires a valve"));
                }
                variable = QStringLiteral("STATUS");
                value = EpanetNetworkBuilderSupport::ruleStatusText(action.status.value());
            }
            else
            {
                if (!std::isfinite(action_setting) || action_setting < 0.0)
                    return makeEpanetStatus(HydraulicSimulationStatusStage::AddRule, HydraulicSimulationStatusOperation::AddRule, HydraulicSimulationStatusEntityType::Rule, rule.id, rule.uuid, QStringLiteral("A control-rule action setting must be finite and non-negative"));
                variable = QStringLiteral("SETTING");
                value = QString::number(action_setting, 'g', 17);
            }

            const QString keyword = action_index == 0 ? first_keyword : QStringLiteral("AND");
            lines.append(QStringLiteral("%1 LINK %2 %3 = %4").arg(keyword, link_id, variable, value));
        }

        return makeEpanetSuccess();
    };

    HydraulicSimulationStatus status = append_actions(rule.actions_then, QStringLiteral("THEN"));
    if (!status.success)
        return status;
    status = append_actions(rule.actions_else, QStringLiteral("ELSE"));
    if (!status.success)
        return status;

    lines.append(QStringLiteral("PRIORITY %1").arg(QString::number(rule.priority, 'g', 17)));
    rule_text = lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
    return makeEpanetSuccess();
}

HydraulicSimulationStatus EpanetNetworkBuilder::addControlRule(const HydraulicControlRule &rule)
{
    QString rule_text;
    const HydraulicSimulationStatus build_status = buildControlRuleText(rule, rule_text);
    if (!build_status.success)
        return build_status;

    int rule_count_before = 0;
    int error = EN_getcount(this->project.handle(), EN_RULECOUNT, &rule_count_before);
    if (error != 0)
    {
        const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddRule, HydraulicSimulationStatusOperation::ResolveEntity, QStringLiteral("EN_getcount(EN_RULECOUNT)"), HydraulicSimulationStatusEntityType::Rule, rule.id, rule.uuid, QStringLiteral("Failed to read the control-rule count before adding a rule"));
        if (!epanet_status.success)
            return epanet_status;
    }

    QByteArray backend_rule_text = rule_text.toUtf8();
    error = EN_addrule(this->project.handle(), backend_rule_text.data());
    if (error != 0)
    {
        const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddRule, HydraulicSimulationStatusOperation::AddRule, QStringLiteral("EN_addrule"), HydraulicSimulationStatusEntityType::Rule, rule.id, rule.uuid, QStringLiteral("Failed to add hydraulic control rule"));
        if (!epanet_status.success)
            return epanet_status;
    }

    int rule_count_after = 0;
    error = EN_getcount(this->project.handle(), EN_RULECOUNT, &rule_count_after);
    if (error != 0 || rule_count_after != rule_count_before + 1)
    {
        if (error != 0)
        {
            const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddRule, HydraulicSimulationStatusOperation::ResolveEntity, QStringLiteral("EN_getcount(EN_RULECOUNT)"), HydraulicSimulationStatusEntityType::Rule, rule.id, rule.uuid, QStringLiteral("Failed to resolve the newly added control-rule index"));
            if (!epanet_status.success)
                return epanet_status;
        }
        return makeEpanetStatus(HydraulicSimulationStatusStage::AddRule, HydraulicSimulationStatusOperation::ResolveEntity, HydraulicSimulationStatusEntityType::Rule, rule.id, rule.uuid, QStringLiteral("EPANET did not add exactly one control rule"));
    }

    const int rule_index = rule_count_after;
    char backend_rule_id[EN_MAXID + 1] = {};
    error = EN_getruleID(this->project.handle(), rule_index, backend_rule_id);
    if (error != 0)
    {
        HydraulicSimulationStatus status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddRule, HydraulicSimulationStatusOperation::ResolveEntity, QStringLiteral("EN_getruleID"), HydraulicSimulationStatusEntityType::Rule, rule.id, rule.uuid, QStringLiteral("Failed to read the newly added control-rule ID"));
        if (!status.success)
        {
            status.entity.index = rule_index;
            return status;
        }
    }
    if (QString::fromUtf8(backend_rule_id) != rule.id)
    {
        EN_deleterule(this->project.handle(), rule_index);
        return makeEpanetStatus(HydraulicSimulationStatusStage::AddRule, HydraulicSimulationStatusOperation::AddRule, HydraulicSimulationStatusEntityType::Rule, rule.id, rule.uuid, QStringLiteral("EPANET returned a control-rule ID different from the model rule ID"));
    }

    error = EN_setruleenabled(this->project.handle(), rule_index, rule.enabled ? EN_TRUE : EN_FALSE);
    if (error != 0)
    {
        HydraulicSimulationStatus status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddRule, HydraulicSimulationStatusOperation::SetEntityMetadata, QStringLiteral("EN_setruleenabled"), HydraulicSimulationStatusEntityType::Rule, rule.id, rule.uuid, QStringLiteral("Failed to set hydraulic control-rule enabled state"));
        if (!status.success)
        {
            status.entity.index = rule_index;
            return status;
        }
    }

    this->indices.controls_rules.insert(rule.uuid, rule_index);
    return makeEpanetSuccess();
}

bool EpanetNetworkBuilder::resolveLinkId(const QUuid &uuid, QString &id) const
{
    id = this->pipe_ids_by_uuid.value(uuid);
    if (id.isEmpty())
        id = this->pump_ids_by_uuid.value(uuid);
    if (id.isEmpty())
        id = this->valve_ids_by_uuid.value(uuid);
    return !id.isEmpty();
}

bool EpanetNetworkBuilder::resolveLinkIndex(const QUuid &uuid, int &index) const
{
    index = this->indices.links_pipes.value(uuid, 0);
    if (index == 0)
        index = this->indices.links_pumps.value(uuid, 0);
    if (index == 0)
        index = this->indices.links_valves.value(uuid, 0);
    return index > 0;
}

bool EpanetNetworkBuilder::resolveControlLinkSetting(const QUuid &link_uuid, const HydraulicControlLinkSetting &setting, double &backend_setting) const
{
    if (EpanetNetworkBuilderSupport::controlLinkSettingValueCount(setting) != 1)
        return false;

    if (this->pump_ids_by_uuid.contains(link_uuid))
    {
        if (!setting.pump_speed_ratio.has_value())
            return false;
        backend_setting = setting.pump_speed_ratio.value();
        return std::isfinite(backend_setting);
    }

    if (!this->valve_types_by_uuid.contains(link_uuid))
        return false;

    switch (this->valve_types_by_uuid.value(link_uuid))
    {
    case HydraulicLinkValveType::PRV:
    case HydraulicLinkValveType::PSV:
    case HydraulicLinkValveType::PBV:
        if (!setting.valve_pressure_head_m.has_value())
            return false;
        backend_setting = setting.valve_pressure_head_m.value();
        return std::isfinite(backend_setting);
    case HydraulicLinkValveType::FCV:
        if (!setting.valve_flow_m3_per_h.has_value())
            return false;
        backend_setting = setting.valve_flow_m3_per_h.value();
        return std::isfinite(backend_setting);
    case HydraulicLinkValveType::TCV:
        if (!setting.valve_loss_coefficient.has_value())
            return false;
        backend_setting = setting.valve_loss_coefficient.value();
        return std::isfinite(backend_setting);
    case HydraulicLinkValveType::PCV:
        if (!setting.valve_position_percent.has_value())
            return false;
        backend_setting = setting.valve_position_percent.value();
        return std::isfinite(backend_setting);
    case HydraulicLinkValveType::GPV:
        return false;
    }

    return false;
}

bool EpanetNetworkBuilder::resolveRulePremiseValue(const HydraulicControlRulePremise &premise, double &value) const
{
    if (EpanetNetworkBuilderSupport::rulePremiseNumericValueCount(premise) != 1)
        return false;

    switch (premise.variable)
    {
    case HydraulicControlRuleVariable::Demand:
        if (!premise.demand_m3_per_h.has_value())
            return false;
        value = premise.demand_m3_per_h.value();
        return true;
    case HydraulicControlRuleVariable::Head:
    case HydraulicControlRuleVariable::Grade:
        if (!premise.hydraulic_head_m.has_value())
            return false;
        value = premise.hydraulic_head_m.value();
        return true;
    case HydraulicControlRuleVariable::Level:
        if (!premise.water_level_m.has_value())
            return false;
        value = premise.water_level_m.value();
        return true;
    case HydraulicControlRuleVariable::Pressure:
        if (!premise.pressure_head_m.has_value())
            return false;
        value = premise.pressure_head_m.value();
        return true;
    case HydraulicControlRuleVariable::Flow:
        if (!premise.flow_m3_per_h.has_value())
            return false;
        value = premise.flow_m3_per_h.value();
        return true;
    case HydraulicControlRuleVariable::Status:
        return false;
    case HydraulicControlRuleVariable::Setting:
        return resolveControlLinkSetting(premise.object_uuid, premise.link_setting, value);
    case HydraulicControlRuleVariable::Power:
        if (!premise.power_kw.has_value())
            return false;
        value = premise.power_kw.value();
        return true;
    case HydraulicControlRuleVariable::Time:
        if (!premise.elapsed_time_s.has_value())
            return false;
        value = static_cast<double>(premise.elapsed_time_s.value());
        return true;
    case HydraulicControlRuleVariable::ClockTime:
        if (!premise.time_of_day_s.has_value())
            return false;
        value = static_cast<double>(premise.time_of_day_s.value());
        return true;
    case HydraulicControlRuleVariable::FillTime:
        if (!premise.fill_time_s.has_value())
            return false;
        value = static_cast<double>(premise.fill_time_s.value());
        return true;
    case HydraulicControlRuleVariable::DrainTime:
        if (!premise.drain_time_s.has_value())
            return false;
        value = static_cast<double>(premise.drain_time_s.value());
        return true;
    }

    return false;
}

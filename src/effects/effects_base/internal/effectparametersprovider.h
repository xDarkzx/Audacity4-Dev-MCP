/*
* Audacity: A Digital Audio Editor
*/
#pragma once

#include <map>

#include "framework/global/modularity/ioc.h"
#include "framework/global/async/asyncable.h"

#include "../ieffectparametersprovider.h"
#include "../ieffectinstancesregister.h"
#include "../ieffectsprovider.h"
#include "../iparameterextractorregistry.h"

namespace au::effects {
class EffectParametersProvider : public IEffectParametersProvider, public muse::async::Asyncable, public muse::Contextable
{
    muse::GlobalInject<IParameterExtractorRegistry> parameterExtractorRegistry;
    muse::GlobalInject<IEffectsProvider> effectsProvider;
    muse::GlobalInject<IEffectInstancesRegister> instancesRegister;

public:
    EffectParametersProvider(const muse::modularity::ContextPtr& ctx);

    // IEffectParametersProvider interface
    ParameterInfoList parameters(EffectInstanceId instanceId) const override;
    ParameterInfo parameter(EffectInstanceId instanceId, const muse::String& parameterId) const override;

    double parameterValue(EffectInstanceId instanceId, const muse::String& parameterId) const override;
    bool setParameterValue(EffectInstanceId instanceId, const muse::String& parameterId, double value) override;
    bool setParameterStringValue(EffectInstanceId instanceId, const muse::String& parameterId, const muse::String& stringValue) override;

    muse::String parameterValueString(EffectInstanceId instanceId, const muse::String& parameterId, double value) const override;

    bool supportsParameterExtraction(const EffectId& effectId) const override;

    void beginParameterGesture(EffectInstanceId instanceId, const muse::String& parameterId) override;
    void endParameterGesture(EffectInstanceId instanceId, const muse::String& parameterId) override;

    muse::async::Channel<ParameterChangedData> parameterChanged() const override;

private:
    // Helper to determine effect family
    EffectFamily getEffectFamily(const EffectId& effectId) const;

    //! Reads the parameter back and emits parameterChanged. Skipped while a gesture is
    //! open on the instance, because both of those reach VST3Wrapper::FetchSettings, which
    //! drains the edits the gesture is still accumulating - see setParameterValue().
    void notifyParameterChanged(EffectInstanceId instanceId, const muse::String& parameterId);

    //! Gestures open per instance, and whether any value was written while they were.
    //! A caller may bracket several parameters at once (see the batched MCP commands),
    //! so this counts rather than flags.
    std::map<EffectInstanceId, int> m_openGestures;
    std::map<EffectInstanceId, muse::String> m_deferredNotify;

    mutable muse::async::Channel<ParameterChangedData> m_parameterChanged;
};
}

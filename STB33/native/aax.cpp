// AAX Native instrument: stereo output, one MIDI input node and the host
// transport. Parameters are normalized AAX parameters; edits reach the engine
// through its queue, and engine-originated panel changes return on the timer.
#include <AAX_Exports.cpp>
#include <AAX_ICollection.h>
#include <AAX_IComponentDescriptor.h>
#include <AAX_IEffectDescriptor.h>
#include <AAX_IPropertyMap.h>
#include <AAX_IMIDINode.h>
#include <AAX_ITransport.h>
#include <AAX_CEffectParameters.h>
#include <AAX_CEffectGUI.h>
#include <AAX_CParameter.h>
#include <AAX_CLinearTaperDelegate.h>
#include <AAX_CNumberDisplayDelegate.h>
#include <AAX_IController.h>
#include <AAX_IViewContainer.h>
#include "dw303.h"
#include "identity.h"
#include <array>
#include <cmath>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class DW303AAX;
struct Algorithm { float** outputs; int32_t* frames; AAX_IMIDINode* midi; AAX_IMIDINode* transport; DW303AAX** owner; };
enum Fields {
    audioOut = AAX_FIELD_INDEX(Algorithm, outputs), length = AAX_FIELD_INDEX(Algorithm, frames),
    midiIn = AAX_FIELD_INDEX(Algorithm, midi), transportIn = AAX_FIELD_INDEX(Algorithm, transport),
    instance = AAX_FIELD_INDEX(Algorithm, owner)
};
constexpr AAX_CTypeID chunkID = 'D303', engineID = 'D3en';
constexpr size_t parameterCount = 12;
constexpr uint32_t maximumMidi = 1024;

static std::string parameterKey(size_t index) {
    Dw303ParamInfo info; dw303_param_info(uint32_t(index), &info);
    return "dw303_" + std::to_string(info.id);
}
static int indexOf(AAX_CParamID id) {
    for (size_t i = 0; i < parameterCount; ++i) if (id && parameterKey(i) == id) return int(i);
    return -1;
}

class DW303AAX final : public AAX_CEffectParameters {
public:
    DW303AAX() : dsp(dw303_create()) {}
    ~DW303AAX() override { dw303_destroy(dsp); }
    static AAX_CEffectParameters* AAX_CALLBACK Create() { return new DW303AAX; }

    AAX_Result EffectInit() override {
        AAX_CSampleRate rate = 0;
        if (!dsp || Controller()->GetSampleRate(&rate) != AAX_SUCCESS || !dw303_prepare(dsp, rate, 4096)) return AAX_ERROR_INVALID_PARAMETER_ID;
        for (size_t i = 0; i < parameterCount; ++i) {
            Dw303ParamInfo info; dw303_param_info(uint32_t(i), &info);
            const auto key = parameterKey(i);
            auto* p = new AAX_CParameter<float>(key.c_str(), AAX_CString(info.name), dw303_param_normalize(uint32_t(i), info.initial),
                AAX_CLinearTaperDelegate<float, 0>(), AAX_CNumberDisplayDelegate<float, 3>(), info.automatable);
            p->SetNumberOfSteps(info.steps ? info.steps + 1 : 2048);
            p->SetType(info.steps ? AAX_eParameterType_Discrete : AAX_eParameterType_Continuous);
            mParameterManager.AddParameter(p);
            reported[i] = dw303_param_normalize(uint32_t(i), info.initial);
        }
        return AAX_SUCCESS;
    }
    AAX_Result ResetFieldData(AAX_CFieldIndex field, void* data, uint32_t size) const override {
        if (field != instance || size != sizeof(DW303AAX*)) return AAX_CEffectParameters::ResetFieldData(field, data, size);
        *static_cast<DW303AAX**>(data) = const_cast<DW303AAX*>(this);
        return AAX_SUCCESS;
    }
    AAX_Result UpdateParameterNormalizedValue(AAX_CParamID id, double normalized, AAX_EUpdateSource source) override {
        if (!id || !std::isfinite(normalized)) return AAX_ERROR_INVALID_PARAMETER_ID;
        auto result = AAX_CEffectParameters::UpdateParameterNormalizedValue(id, normalized, source);
        const int index = indexOf(id);
        if (result == AAX_SUCCESS && index >= 0) {
            reported[size_t(index)] = normalized;
            if (!reflecting) dw303_set_value(dsp, uint32_t(index), dw303_param_denormalize(uint32_t(index), float(normalized)));
        }
        return result;
    }
    // Copy panel and sequencer changes made by the engine into the host values.
    AAX_Result TimerWakeup() override {
        reflecting = true;
        for (size_t i = 0; i < parameterCount; ++i) {
            const double now = dw303_param_normalize(uint32_t(i), dw303_value(dsp, uint32_t(i)));
            if (std::fabs(now - reported[i]) > 1e-6) SetParameterNormalizedValue(parameterKey(i).c_str(), now);
        }
        reflecting = false;
        return AAX_CEffectParameters::TimerWakeup();
    }
    AAX_Result GetParameterStringFromValue(AAX_CParamID id, double value, AAX_IString* text, int32_t maximum) const override {
        const int index = indexOf(id); char buffer[64];
        if (index < 0 || !dw303_param_text(uint32_t(index), dw303_param_denormalize(uint32_t(index), float(value)), buffer, sizeof(buffer)))
            return AAX_CEffectParameters::GetParameterStringFromValue(id, value, text, maximum);
        *text = AAX_CString(buffer);
        return AAX_SUCCESS;
    }
    AAX_Result GetParameterValueFromString(AAX_CParamID id, double* value, const AAX_IString& text) const override {
        const int index = indexOf(id); float physical = 0;
        if (index < 0 || !dw303_param_parse(uint32_t(index), text.Get(), &physical)) return AAX_CEffectParameters::GetParameterValueFromString(id, value, text);
        *value = dw303_param_normalize(uint32_t(index), physical);
        return AAX_SUCCESS;
    }

    AAX_Result GetNumberOfChunks(int32_t* count) const override { *count = 2; return AAX_SUCCESS; }
    AAX_Result GetChunkIDFromIndex(int32_t index, AAX_CTypeID* id) const override {
        if (index == 1) { *id = chunkID; return AAX_SUCCESS; }
        return AAX_CEffectParameters::GetChunkIDFromIndex(index, id);
    }
    AAX_Result GetChunkSize(AAX_CTypeID id, uint32_t* size) const override {
        if (id != chunkID) return AAX_CEffectParameters::GetChunkSize(id, size);
        size_t length = 0; auto* bytes = dw303_state_save(dsp, &length);
        if (!bytes) return AAX_ERROR_INVALID_CHUNK_ID;
        std::lock_guard<std::mutex> lock(chunkMutex);
        cached[std::this_thread::get_id()].assign(bytes, bytes + length);
        dw303_bytes_free(bytes, length);
        *size = uint32_t(length);
        return AAX_SUCCESS;
    }
    AAX_Result GetChunk(AAX_CTypeID id, AAX_SPlugInChunk* chunk) const override {
        if (id != chunkID) return AAX_CEffectParameters::GetChunk(id, chunk);
        std::lock_guard<std::mutex> lock(chunkMutex);
        auto found = cached.find(std::this_thread::get_id());
        if (found == cached.end()) return AAX_ERROR_INVALID_CHUNK_ID;
        chunk->fSize = int32_t(found->second.size());
        std::memcpy(chunk->fData, found->second.data(), found->second.size());
        cached.erase(found);
        return AAX_SUCCESS;
    }
    AAX_Result SetChunk(AAX_CTypeID id, const AAX_SPlugInChunk* chunk) override {
        if (id != chunkID) return AAX_CEffectParameters::SetChunk(id, chunk);
        if (chunk->fSize < 0 || !dw303_state_load(dsp, reinterpret_cast<const uint8_t*>(chunk->fData), size_t(chunk->fSize))) return AAX_ERROR_INVALID_CHUNK_ID;
        reflecting = true;
        for (size_t i = 0; i < parameterCount; ++i)
            SetParameterNormalizedValue(parameterKey(i).c_str(), dw303_param_normalize(uint32_t(i), dw303_value(dsp, uint32_t(i))));
        reflecting = false;
        return AAX_SUCCESS;
    }
    // The editor runs in the same process and attaches to this engine.
    AAX_Result GetCustomData(AAX_CTypeID id, uint32_t size, void* data, uint32_t* written) const override {
        if (id != engineID || size != sizeof(Dw303*)) return AAX_CEffectParameters::GetCustomData(id, size, data, written);
        *static_cast<Dw303**>(data) = dsp;
        if (written) *written = sizeof(Dw303*);
        return AAX_SUCCESS;
    }

    void process(Algorithm& data) {
        const int32_t frames = *data.frames;
        if (frames <= 0) return;
        uint32_t midiCount = 0;
        if (auto* stream = data.midi ? data.midi->GetNodeBuffer() : nullptr) {
            for (uint32_t i = 0; i < stream->mBufferSize && midiCount < maximumMidi; ++i) {
                const auto& packet = stream->mBuffer[i];
                if (packet.mLength < 1 || packet.mLength > 3) continue;
                midi[midiCount] = {packet.mTimestamp, packet.mLength, {packet.mData[0], packet.mData[1], packet.mData[2], 0}};
                ++midiCount;
            }
        }
        Dw303Transport transport{};
        if (auto* t = data.transport ? data.transport->GetTransport() : nullptr) {
            bool playing = false; double tempo = 0; int64_t ticks = 0, sample = 0; uint32_t perQuarter = 0;
            if (t->IsTransportPlaying(&playing) == AAX_SUCCESS) transport.flags |= DW303_TRANSPORT_PLAYING_VALID | (playing ? DW303_TRANSPORT_PLAYING : 0);
            if (t->GetCurrentTempo(&tempo) == AAX_SUCCESS) { transport.flags |= DW303_TRANSPORT_TEMPO; transport.tempo = tempo; }
            if (t->GetCurrentTickPosition(&ticks) == AAX_SUCCESS && t->GetTicksPerQuarter(&perQuarter) == AAX_SUCCESS && perQuarter) {
                transport.flags |= DW303_TRANSPORT_BEATS; transport.beats = double(ticks) / perQuarter;
            }
            if (t->GetCurrentNativeSampleLocation(&sample) == AAX_SUCCESS) { transport.flags |= DW303_TRANSPORT_SAMPLE; transport.sample = sample; }
        }
        for (uint32_t i = 1; i < midiCount; ++i)
            for (uint32_t j = i; j > 0 && midi[j - 1].frame > midi[j].frame; --j) std::swap(midi[j - 1], midi[j]);
        dw303_process(dsp, data.outputs[0], data.outputs[1], uint32_t(frames), midi.data(), midiCount, nullptr, 0, &transport);
    }
private:
    Dw303* dsp;
    bool reflecting = false;
    std::array<double, parameterCount> reported{};
    std::array<Dw303Midi, maximumMidi> midi{};
    mutable std::mutex chunkMutex;
    mutable std::map<std::thread::id, std::vector<uint8_t>> cached;
};

static void AAX_CALLBACK algorithm(Algorithm* const begin[], const void* const end) {
    for (auto* current = begin; current != end; ++current)
        if (*current && (*current)->owner && *(*current)->owner) (*(*current)->owner)->process(**current);
}

class DW303AAXGUI final : public AAX_CEffectGUI {
public:
    static AAX_IEffectGUI* AAX_CALLBACK Create() { return new DW303AAXGUI; }
    ~DW303AAXGUI() override { DeleteViewContainer(); }
    void CreateViewContents() override {}
    void CreateViewContainer() override {
        if (GetViewContainerType() != AAX_eViewContainer_Type_NSView || !GetEffectParameters()) return;
        Dw303* dsp = nullptr; uint32_t written = 0;
        if (GetEffectParameters()->GetCustomData(engineID, sizeof(dsp), &dsp, &written) != AAX_SUCCESS || !dsp) return;
        Dw303EditorHost host{this,
            [](void* c, uint32_t i) { static_cast<DW303AAXGUI*>(c)->gesture(i, 0, 0); },
            [](void* c, uint32_t i, float v) { static_cast<DW303AAXGUI*>(c)->gesture(i, 1, v); },
            [](void* c, uint32_t i) { static_cast<DW303AAXGUI*>(c)->gesture(i, 2, 0); }};
        editor = dw303_editor_create(dsp, host);
        if (editor && !dw303_editor_attach(editor, GetViewContainerPtr())) DeleteViewContainer();
    }
    void DeleteViewContainer() override { if (editor) { dw303_editor_destroy(editor); editor = nullptr; } }
    AAX_Result GetViewSize(AAX_Point* size) const override {
        uint32_t w = 0, h = 0; dw303_editor_default_size(&w, &h);
        size->horz = float(w); size->vert = float(h);
        return AAX_SUCCESS;
    }
private:
    void gesture(uint32_t index, int kind, float value) {
        auto* params = GetEffectParameters(); if (!params) return;
        const auto key = parameterKey(index);
        if (kind == 0) params->TouchParameter(key.c_str());
        else if (kind == 2) params->ReleaseParameter(key.c_str());
        else params->SetParameterNormalizedValue(key.c_str(), dw303_param_normalize(index, value));
    }
    Dw303Editor* editor = nullptr;
};

AAX_Result GetEffectDescriptions(AAX_ICollection* collection) {
    auto* descriptor = collection->NewDescriptor();
    if (!descriptor) return AAX_ERROR_NULL_OBJECT;
    descriptor->AddName(DW303_NAME);
    descriptor->AddCategory(AAX_ePlugInCategory_SWGenerators);
    descriptor->AddProcPtr(reinterpret_cast<void*>(DW303AAX::Create), kAAX_ProcPtrID_Create_EffectParameters);
    descriptor->AddProcPtr(reinterpret_cast<void*>(DW303AAXGUI::Create), kAAX_ProcPtrID_Create_EffectGUI);
    auto* component = descriptor->NewComponentDescriptor();
    if (!component) return AAX_ERROR_NULL_OBJECT;
    component->AddAudioOut(audioOut);
    component->AddAudioBufferLength(length);
    component->AddMIDINode(midiIn, AAX_eMIDINodeType_LocalInput, DW303_NAME, 0xffff);
    component->AddMIDINode(transportIn, AAX_eMIDINodeType_Transport, "Transport", 0xffff);
    component->AddPrivateData(instance, sizeof(DW303AAX*));
    auto* properties = component->NewPropertyMap();
    if (!properties) return AAX_ERROR_NULL_OBJECT;
    properties->AddProperty(AAX_eProperty_ManufacturerID, DW303_MANUFACTURER_CODE);
    properties->AddProperty(AAX_eProperty_ProductID, DW303_PRODUCT_CODE);
    properties->AddProperty(AAX_eProperty_PlugInID_Native, 'D3n2');
    properties->AddProperty(AAX_eProperty_InputStemFormat, AAX_eStemFormat_None);
    properties->AddProperty(AAX_eProperty_OutputStemFormat, AAX_eStemFormat_Stereo);
    properties->AddProperty(AAX_eProperty_CanBypass, true);
    properties->AddProperty(AAX_eProperty_UsesClientGUI, false);
    properties->AddProperty(AAX_eProperty_ObservesTransportState, true);
    auto result = component->AddProcessProc_Native(algorithm, properties);
    if (result != AAX_SUCCESS) return result;
    result = descriptor->AddComponent(component);
    if (result != AAX_SUCCESS) return result;
    result = collection->AddEffect(DW303_BUNDLE_ID ".aax", descriptor);
    if (result != AAX_SUCCESS) return result;
    collection->SetManufacturerName(DW303_COMPANY);
    collection->AddPackageName(DW303_NAME);
    collection->SetPackageVersion(DW303_VERSION_INT);
    return AAX_SUCCESS;
}

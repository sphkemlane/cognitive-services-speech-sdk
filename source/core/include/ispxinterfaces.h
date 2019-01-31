//
// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE.md file in the project root for full license information.
//
// ISpxInterfaces.h: Implementation declarations for all ISpx* C++ interface classes
//

#pragma once
#include <memory>
#include <vector>
#include <map>
#include <chrono>
#include "spxcore_common.h"
#include "audio_chunk.h"
#include "platform.h"
#include "asyncop.h"
#include <speechapi_cxx_common.h>
#include "speechapi_cxx_eventsignal.h"
#include "speechapi_cxx_enums.h"
#include "shared_ptr_helpers.h"
#include "spxdebug.h"

using namespace Microsoft::CognitiveServices::Speech;
using namespace Microsoft::CognitiveServices::Speech::Translation;

namespace Microsoft {
namespace CognitiveServices {
namespace Speech {
namespace Impl {

class ISpxInterfaceBase : public std::enable_shared_from_this<ISpxInterfaceBase>
{
public:

    virtual ~ISpxInterfaceBase() = default;

    template <class I>
    std::shared_ptr<I> QueryInterface()
    {
        return QueryInterfaceInternal<I>();
    }

protected:

    template <class I>
    std::shared_ptr<I> QueryInterfaceInternal()
    {
        // try to query for the interface via our virtual method...
        auto ptr = QueryInterface(PAL::GetTypeName<I>().c_str());
        if (ptr != nullptr)
        {
            auto interfacePtr = reinterpret_cast<I*>(ptr);
            return interfacePtr->shared_from_this();
        }

        // if that fails, let the caller know
        return nullptr;
    }

    virtual void* QueryInterface(const char* /*interfaceName*/) { return nullptr; }

    typedef std::enable_shared_from_this<ISpxInterfaceBase> base_type;

    std::shared_ptr<ISpxInterfaceBase> shared_from_this()
    {
        return base_type::shared_from_this();
    }
};

template <class I>
std::shared_ptr<I> SpxQueryInterface(std::shared_ptr<ISpxInterfaceBase> from)
{
    if (from != nullptr)
    {
        #if defined(_MSC_VER) && defined(_DEBUG)
            std::shared_ptr<I> ptr1 = std::dynamic_pointer_cast<I>(from);
            std::shared_ptr<I> ptr2 = from->QueryInterface<I>();
            SPX_DBG_TRACE_ERROR_IF(ptr1 != nullptr && ptr2 == nullptr, "dynamic_pointer_cast() and QueryInterface() do not agree!! UNEXPECTED!");
            SPX_DBG_TRACE_ERROR_IF(ptr1 == nullptr && ptr2 != nullptr, "dynamic_pointer_cast() and QueryInterface() do not agree!! UNEXPECTED!");
            SPX_IFTRUE_THROW_HR(ptr1 != nullptr && ptr2 == nullptr, SPXERR_ABORT);
            SPX_IFTRUE_THROW_HR(ptr1 == nullptr && ptr2 != nullptr, SPXERR_ABORT);
            return ptr1;
        #elif defined(_MSC_VER)
            std::shared_ptr<I> ptr = std::dynamic_pointer_cast<I>(from);
            return ptr != nullptr ? ptr : from->QueryInterface<I>();
        #else
            std::shared_ptr<I> ptr = from->QueryInterface<I>();
            return ptr != nullptr ? ptr : std::dynamic_pointer_cast<I>(from);
        #endif
    }
    return nullptr;
}


template<typename T>
struct ISpxInterfaceBaseFor : virtual public ISpxInterfaceBase
{
public:
    virtual ~ISpxInterfaceBaseFor() = default;

    std::shared_ptr<T> shared_from_this()
    {
        std::shared_ptr<T> result(base_type::shared_from_this(), static_cast<T*>(this));
        return result;
    }


private:
    typedef ISpxInterfaceBase base_type;

    ISpxInterfaceBaseFor&& operator =(const ISpxInterfaceBaseFor&&) = delete;
};

class ISpxObjectInit : public ISpxInterfaceBaseFor<ISpxObjectInit>
{
public:
    virtual void Init() = 0;
    virtual void Term() = 0;
};

class ISpxGenericSite : public ISpxInterfaceBaseFor<ISpxGenericSite>
{
};

class ISpxObjectWithSite : public ISpxInterfaceBaseFor<ISpxObjectWithSite>
{
public:
    virtual void SetSite(std::weak_ptr<ISpxGenericSite> site) = 0;
};

template <class T, class I, class... Types>
inline std::shared_ptr<I> SpxCreateObjectInternal(Types&&... Args)
{
    SPX_DBG_TRACE_VERBOSE("Creating object via %s: %s as %s", __FUNCTION__, PAL::GetTypeName<T>(), PAL::GetTypeName<I>());
    std::shared_ptr<T> ptr = std::make_shared<T>(std::forward<Types>(Args)...);
    auto it = std::dynamic_pointer_cast<I>(ptr);
    return it;
}


class ISpxObjectFactory : public ISpxInterfaceBaseFor<ISpxObjectFactory>
{
public:
    template <class I>
    std::shared_ptr<I> CreateObject(const char* className)
    {
        // try to create the object from our interface virtual method...
        auto obj = CreateObject(className, PAL::GetTypeName<I>().c_str());
        if (obj != nullptr)
        {
            auto ptr = reinterpret_cast<I*>(obj);
            return std::shared_ptr<I>(ptr);
        }

        // if we can't, return nullptr to let the caller know
        return nullptr;
    }

    template <class T, class I>
    std::shared_ptr<I> CreateObject()
    {
        auto obj = CreateObject<I>(PAL::GetTypeName<T>());
        if (obj != nullptr)
        {
            return obj;
        }

        // if that didn't work, just go ahead and delegate to our internal helper for this module
        return SpxCreateObjectInternal<T, I>();
    }

    virtual void* CreateObject(const char* className, const char* interfaceName) = 0;
};

template <class T>
class ISpxObjectWithSiteInitImpl : public ISpxObjectWithSite, public ISpxObjectInit
{
public:
    // --- ISpxObjectWithSite
    void SetSite(std::weak_ptr<ISpxGenericSite> site) override
    {
        auto shared = site.lock();
        auto ptr = SpxQueryInterface<T>(shared);
        SPX_IFFALSE_THROW_HR((bool)ptr == (bool)shared, SPXERR_INVALID_ARG);

        if (m_hasSite)
        {
            Term();
            m_site.reset();
            m_hasSite = false;
        }

        m_site = ptr;
        m_hasSite = ptr.get() != nullptr;

        if (m_hasSite)
        {
            Init();
        }
    }

    // --- ISpxObjectInit
    void Init() override
    {
    }

    void Term() override
    {
    }

protected:
    ISpxObjectWithSiteInitImpl() : m_hasSite(false) {}

    std::shared_ptr<T> GetSite() const
    {
        return m_site.lock();
    }

    template<class F>
    void InvokeOnSite(F f)
    {
        auto site = GetSite();
        if (site != nullptr)
        {
            f(site);
        }
    }


private:
    bool m_hasSite;
    mutable std::weak_ptr<T> m_site;
};

class ISpxServiceProvider : public ISpxInterfaceBaseFor<ISpxServiceProvider>
{
public:
    virtual std::shared_ptr<ISpxInterfaceBase> QueryService(const char* serviceName) = 0;
};


class ISpxAddServiceProvider : public ISpxInterfaceBaseFor<ISpxAddServiceProvider>
{
public:
    template <class T>
    void AddService(std::shared_ptr<T> service)
    {
        AddService(PAL::GetTypeName<T>(), service);
    }

    virtual void AddService(const char* serviceName, std::shared_ptr<ISpxInterfaceBase> service) = 0;
};


#pragma pack (push, 1)
struct SPXWAVEFORMAT
{
    uint16_t wFormatTag;        /* format type */
    uint16_t nChannels;         /* number of channels (i.e. mono, stereo...) */
    uint32_t nSamplesPerSec;    /* sample rate */
    uint32_t nAvgBytesPerSec;   /* for buffer estimation */
    uint16_t nBlockAlign;       /* block size of data */
    uint16_t wBitsPerSample;    /* Number of bits per sample of mono data */
};

struct SPXWAVEFORMATEX
{
    uint16_t wFormatTag;        /* format type */
    uint16_t nChannels;         /* number of channels (i.e. mono, stereo...) */
    uint32_t nSamplesPerSec;    /* sample rate */
    uint32_t nAvgBytesPerSec;   /* for buffer estimation */
    uint16_t nBlockAlign;       /* block size of data */
    uint16_t wBitsPerSample;    /* Number of bits per sample of mono data */
    uint16_t cbSize;            /* The count in bytes of the size of extra information (after cbSize) */
};
#pragma pack (pop)

using SpxWAVEFORMATEX_Type = std::shared_ptr<SPXWAVEFORMATEX>;
inline SpxWAVEFORMATEX_Type SpxAllocWAVEFORMATEX(size_t sizeInBytes)
{
    return SpxAllocSharedBuffer<SPXWAVEFORMATEX>(sizeInBytes);
}

using SpxSharedAudioBuffer_Type = SpxSharedUint8Buffer_Type;
inline SpxSharedAudioBuffer_Type SpxAllocSharedAudioBuffer(size_t sizeInBytes)
{
    return SpxAllocSharedUint8Buffer(sizeInBytes);
}

class ISpxAudioStream : public ISpxInterfaceBaseFor<ISpxAudioStream>
{
public:
    virtual uint16_t GetFormat(SPXWAVEFORMATEX* pformat, uint16_t cbFormat) = 0;
};

class ISpxAudioStreamInitFormat : public ISpxInterfaceBaseFor<ISpxAudioStreamInitFormat>
{
public:
    virtual void SetFormat(SPXWAVEFORMATEX* format) = 0;
};

class ISpxAudioStreamInitRealTime : public ISpxInterfaceBaseFor<ISpxAudioStreamInitRealTime>
{
public:
    virtual void SetRealTimePercentage(uint8_t percentage) = 0;
};

class ISpxAudioStreamReaderInitCallbacks : public ISpxInterfaceBaseFor<ISpxAudioStreamReaderInitCallbacks>
{
public:
    using ReadCallbackFunction_Type = std::function<int(uint8_t*, uint32_t)>;
    using CloseCallbackFunction_Type = std::function<void()>;

    virtual void SetCallbacks(ReadCallbackFunction_Type readCallback, CloseCallbackFunction_Type closeCallback) = 0;
};

class ISpxAudioStreamReader : public ISpxInterfaceBaseFor<ISpxAudioStreamReader>
{
public:
    virtual uint16_t GetFormat(SPXWAVEFORMATEX* pformat, uint16_t cbFormat) = 0;
    virtual uint32_t Read(uint8_t* pbuffer, uint32_t cbBuffer) = 0;
    virtual void Close() = 0;
};

class ISpxAudioStreamWriter : public ISpxInterfaceBaseFor<ISpxAudioStreamWriter>
{
public:
    virtual void Write(uint8_t* buffer, uint32_t size) = 0;
};

class ISpxAudioFile : public ISpxInterfaceBaseFor<ISpxAudioFile>
{
public:
    virtual void Open(const wchar_t* pszFileName) = 0;
    virtual void Close() = 0;

    virtual bool IsOpen() const = 0;

    virtual void SetContinuousLoop(bool value) = 0;
    virtual void SetIterativeLoop(bool value) = 0;
};

class ISpxAudioConfig : public ISpxInterfaceBaseFor<ISpxAudioConfig>
{
public:
    virtual void InitFromDefaultDevice() = 0;
    virtual void InitFromFile(const wchar_t* pszFileName) = 0;
    virtual void InitFromStream(std::shared_ptr<ISpxAudioStream> stream) = 0;

    virtual std::wstring GetFileName() const = 0;
    virtual std::shared_ptr<ISpxAudioStream> GetStream() = 0;
};

class ISpxAudioProcessor : public ISpxInterfaceBaseFor<ISpxAudioProcessor>
{
public:

    virtual void SetFormat(const SPXWAVEFORMATEX* pformat) = 0;
    virtual void ProcessAudio(const DataChunkPtr& audioChunk) = 0;
};

class ISpxAudioPump : public ISpxInterfaceBaseFor<ISpxAudioPump>
{
public:
    virtual uint16_t GetFormat(SPXWAVEFORMATEX* pformat, uint16_t cbFormat) = 0;
    virtual void SetFormat(const SPXWAVEFORMATEX* pformat, uint16_t cbFormat) = 0;

    virtual void StartPump(std::shared_ptr<ISpxAudioProcessor> pISpxAudioProcessor) = 0;
    virtual void PausePump() = 0;
    virtual void StopPump() = 0;

    enum class State { NoInput, Idle, Paused, Processing };
    virtual State GetState() = 0;
};

class ISpxAudioPumpInit : public ISpxInterfaceBaseFor<ISpxAudioPumpInit>
{
public:
    virtual void SetReader(std::shared_ptr<ISpxAudioStreamReader> reader) = 0;
};

class ISpxKwsModel : public ISpxInterfaceBaseFor<ISpxKwsModel>
{
public:
    virtual void InitFromFile(const wchar_t* fileName) = 0;
    virtual std::wstring GetFileName() const = 0;
};

#define REASON_CANCELED_NONE static_cast<CancellationReason>(0)
#define NO_MATCH_REASON_NONE static_cast<NoMatchReason>(0)

class ISpxRecognitionResult : public ISpxInterfaceBaseFor<ISpxRecognitionResult>
{
public:
    virtual std::wstring GetResultId() = 0;
    virtual std::wstring GetText() = 0;

    virtual ResultReason GetReason() = 0;
    virtual CancellationReason GetCancellationReason() = 0;
    virtual CancellationErrorCode GetCancellationErrorCode() = 0;
    virtual NoMatchReason GetNoMatchReason() = 0;

    virtual uint64_t GetOffset() const = 0;
    virtual void SetOffset(uint64_t) = 0;
    virtual uint64_t GetDuration() const = 0;
};

class ISpxRecognitionResultInit : public ISpxInterfaceBaseFor<ISpxRecognitionResultInit>
{
public:

    virtual void InitIntermediateResult(const wchar_t* resultId, const wchar_t* text, uint64_t offset, uint64_t duration) = 0;
    virtual void InitFinalResult(const wchar_t* resultId, ResultReason reason, NoMatchReason noMatchReason, CancellationReason cancellation, CancellationErrorCode errorCode, const wchar_t* text, uint64_t offset, uint64_t duration) = 0;
};

class ISpxRecognizer : public ISpxInterfaceBaseFor<ISpxRecognizer>
{
public:
    virtual bool IsEnabled() = 0;
    virtual void Enable() = 0;
    virtual void Disable() = 0;

    virtual CSpxAsyncOp<std::shared_ptr<ISpxRecognitionResult>> RecognizeAsync() = 0;
    virtual CSpxAsyncOp<void> StartContinuousRecognitionAsync() = 0;
    virtual CSpxAsyncOp<void> StopContinuousRecognitionAsync() = 0;

    virtual CSpxAsyncOp<void> StartKeywordRecognitionAsync(std::shared_ptr<ISpxKwsModel> model) = 0;
    virtual CSpxAsyncOp<void> StopKeywordRecognitionAsync() = 0;

    virtual void OpenConnection(bool forContinuousRecognition) = 0;
    virtual void CloseConnection() = 0;
};

class ISpxConnection : public ISpxInterfaceBaseFor<ISpxConnection>
{
public:
    virtual void Open(bool forContinuousRecognition) = 0;
    virtual void Close() = 0;
    virtual std::shared_ptr<ISpxRecognizer> GetRecognizer() = 0;
};

class ISpxConnectionInit : public ISpxInterfaceBaseFor<ISpxConnectionInit>
{
public:
    virtual void Init(std::weak_ptr<ISpxRecognizer> recognizer) = 0;
};

class ISpxConnectionFromRecognizer : public ISpxInterfaceBaseFor<ISpxConnectionFromRecognizer>
{
public:
    virtual std::shared_ptr<ISpxConnection> GetConnection() = 0;
};

class ISpxSessionEventArgs : public ISpxInterfaceBaseFor<ISpxSessionEventArgs>
{
public:
    virtual const std::wstring& GetSessionId() = 0;
};

class ISpxSessionEventArgsInit : public ISpxInterfaceBaseFor<ISpxSessionEventArgsInit>
{
public:
    virtual void Init(const std::wstring& sessionId) = 0;
};

class ISpxConnectionEventArgs :
    public ISpxSessionEventArgs,
    public ISpxInterfaceBaseFor<ISpxConnectionEventArgs>
{
};

class ISpxConnectionEventArgsInit : public ISpxInterfaceBaseFor<ISpxConnectionEventArgsInit>
{
public:
    virtual void Init(const std::wstring& sessionId) = 0;
};

class ISpxRecognitionEventArgs :
    public ISpxSessionEventArgs,
    public ISpxInterfaceBaseFor<ISpxRecognitionEventArgs>
{
public:
    virtual const uint64_t& GetOffset() = 0;
    virtual std::shared_ptr<ISpxRecognitionResult> GetResult() = 0;
};

class ISpxRecognitionEventArgsInit : public ISpxInterfaceBaseFor<ISpxRecognitionEventArgsInit>
{
public:
    virtual void Init(const std::wstring& sessionId, std::shared_ptr<ISpxRecognitionResult> result) = 0;
    virtual void Init(const std::wstring& sessionId, uint64_t offset) = 0;
};


class ISpxRecognizerEvents : public ISpxInterfaceBaseFor<ISpxRecognizerEvents>
{
public:
    using RecoEvent_Type = EventSignal<std::shared_ptr<ISpxRecognitionEventArgs>>;
    using SessionEvent_Type = EventSignal<std::shared_ptr<ISpxSessionEventArgs>>;
    using ConnectionEvent_Type = EventSignal<std::shared_ptr<ISpxConnectionEventArgs>>;

    virtual void FireSessionStarted(const std::wstring& sessionId) = 0;
    virtual void FireSessionStopped(const std::wstring& sessionId) = 0;

    virtual void FireConnected(const std::wstring& sessionId) = 0;
    virtual void FireDisconnected(const std::wstring& sessionId) = 0;

    virtual void FireSpeechStartDetected(const std::wstring& sessionId, uint64_t offset) = 0;
    virtual void FireSpeechEndDetected(const std::wstring& sessionId, uint64_t offset) = 0;

    virtual void FireResultEvent(const std::wstring& sessionId, std::shared_ptr<ISpxRecognitionResult> result) = 0;

    SessionEvent_Type SessionStarted;
    SessionEvent_Type SessionStopped;

    ConnectionEvent_Type Connected;
    ConnectionEvent_Type Disconnected;

    RecoEvent_Type SpeechStartDetected;
    RecoEvent_Type SpeechEndDetected;

    RecoEvent_Type IntermediateResult;
    RecoEvent_Type FinalResult;
    RecoEvent_Type Canceled;
    RecoEvent_Type TranslationSynthesisResult;

protected:
    ISpxRecognizerEvents(RecoEvent_Type::NotifyCallback_Type connectedCallback, RecoEvent_Type::NotifyCallback_Type disconnectedCallback) :
        SessionStarted(nullptr, nullptr, true),
        SessionStopped(nullptr, nullptr, true),
        Connected(nullptr, nullptr, true),
        Disconnected(nullptr, nullptr, true),
        SpeechStartDetected(connectedCallback, disconnectedCallback, true),
        SpeechEndDetected(connectedCallback, disconnectedCallback, true),
        IntermediateResult(connectedCallback, disconnectedCallback, true),
        FinalResult(connectedCallback, disconnectedCallback, true),
        Canceled(connectedCallback, disconnectedCallback, true),
        TranslationSynthesisResult(connectedCallback, disconnectedCallback, true)
    {
    }

private:
    ISpxRecognizerEvents() = delete;
};

class ISpxSession : public ISpxInterfaceBaseFor<ISpxSession>
{
public:
    virtual const std::wstring& GetSessionId() const = 0;

    virtual void AddRecognizer(std::shared_ptr<ISpxRecognizer> recognizer) = 0;
    virtual void RemoveRecognizer(ISpxRecognizer* recognizer) = 0;

    virtual CSpxAsyncOp<std::shared_ptr<ISpxRecognitionResult>> RecognizeAsync() = 0;
    virtual CSpxAsyncOp<void> StartContinuousRecognitionAsync() = 0;
    virtual CSpxAsyncOp<void> StopContinuousRecognitionAsync() = 0;

    virtual CSpxAsyncOp<void> StartKeywordRecognitionAsync(std::shared_ptr<ISpxKwsModel> model) = 0;
    virtual CSpxAsyncOp<void> StopKeywordRecognitionAsync() = 0;

    virtual void OpenConnection(bool forContinuousRecognition) = 0;
    virtual void CloseConnection() = 0;
};

class ISpxAudioStreamSessionInit : public ISpxInterfaceBaseFor<ISpxAudioStreamSessionInit>
{
public:
    virtual void InitFromFile(const wchar_t* pszFileName) = 0;
    virtual void InitFromMicrophone() = 0;
    virtual void InitFromStream(std::shared_ptr<ISpxAudioStream> stream) = 0;
};

class ISpxSessionFromRecognizer : public ISpxInterfaceBaseFor<ISpxSessionFromRecognizer>
{
public:
    virtual std::shared_ptr<ISpxSession> GetDefaultSession() = 0;
};

class ISpxRecoEngineAdapter :
    public ISpxAudioProcessor,
    public ISpxInterfaceBaseFor<ISpxRecoEngineAdapter>
{
public:
    virtual void SetAdapterMode(bool singleShot) = 0;
    virtual void OpenConnection(bool) {};
    virtual void CloseConnection() {};
};

class SpxRecoEngineAdapterError
{
    bool m_isTransportError;
    CancellationReason m_reason;
    CancellationErrorCode m_errorCode;
    std::string m_info;

public:
    SpxRecoEngineAdapterError(bool isTransportError, CancellationReason reason, CancellationErrorCode errorCode, const std::string& info)
        : m_isTransportError{ isTransportError }, m_reason{ reason }, m_errorCode{ errorCode }, m_info{ info }
    {}

    bool IsTransportError() const
    {
        return m_isTransportError;
    }

    const std::string& Info() const
    {
        return m_info;
    }

    CancellationReason Reason() const
    {
        return m_reason;
    }

    CancellationErrorCode ErrorCode() const
    {
        return m_errorCode;
    }
};

class ISpxRecoEngineAdapterSite : public ISpxInterfaceBaseFor<ISpxRecoEngineAdapterSite>
{
public:
    using ResultPayload_Type = std::shared_ptr<ISpxRecognitionResult>;
    using AdditionalMessagePayload_Type = void*;
    using ErrorPayload_Type = std::shared_ptr<SpxRecoEngineAdapterError>;

    virtual void GetScenarioCount(uint16_t* countSpeech, uint16_t* countIntent, uint16_t* countTranslation) = 0;

    virtual std::list<std::string> GetListenForList() = 0;
    virtual void GetIntentInfo(std::string& provider, std::string& id, std::string& key, std::string& region) = 0;

    virtual void AdapterStartingTurn(ISpxRecoEngineAdapter* adapter) = 0;
    virtual void AdapterStartedTurn(ISpxRecoEngineAdapter* adapter, const std::string& id) = 0;
    virtual void AdapterStoppedTurn(ISpxRecoEngineAdapter* adapter) = 0;

    virtual void AdapterDetectedSpeechStart(ISpxRecoEngineAdapter* adapter, uint64_t offset) = 0;
    virtual void AdapterDetectedSpeechEnd(ISpxRecoEngineAdapter* adapter, uint64_t offset) = 0;

    virtual void AdapterDetectedSoundStart(ISpxRecoEngineAdapter* adapter, uint64_t offset) = 0;
    virtual void AdapterDetectedSoundEnd(ISpxRecoEngineAdapter* adapter, uint64_t offset) = 0;

    virtual void FireAdapterResult_Intermediate(ISpxRecoEngineAdapter* adapter, uint64_t offset, ResultPayload_Type payload) = 0;
    virtual void FireAdapterResult_FinalResult(ISpxRecoEngineAdapter* adapter, uint64_t offset, ResultPayload_Type payload) = 0;
    virtual void FireAdapterResult_TranslationSynthesis(ISpxRecoEngineAdapter* adapter, ResultPayload_Type payload) = 0;
    virtual void AdapterEndOfDictation(ISpxRecoEngineAdapter* adapter, uint64_t offset, uint64_t duration) = 0;
    virtual void FireConnectedEvent() = 0;
    virtual void FireDisconnectedEvent() = 0;

    virtual void AdapterRequestingAudioMute(ISpxRecoEngineAdapter* adapter, bool mute) = 0;
    virtual void AdapterCompletedSetFormatStop(ISpxRecoEngineAdapter* adapter) = 0;

    virtual void AdditionalMessage(ISpxRecoEngineAdapter* adapter, uint64_t offset, AdditionalMessagePayload_Type payload) = 0;

    virtual void Error(ISpxRecoEngineAdapter* adapter, ErrorPayload_Type payload) = 0;
};

class ISpxAudioPumpSite : public ISpxInterfaceBaseFor<ISpxAudioPumpSite>
{
public:
    virtual void Error(const std::string& msg) = 0;
};

class ISpxKwsEngineAdapter :
    public ISpxAudioProcessor,
    public ISpxInterfaceBaseFor<ISpxKwsEngineAdapter>
{
};

class ISpxKwsEngineAdapterSite : public ISpxInterfaceBaseFor<ISpxKwsEngineAdapterSite>
{
public:
    virtual void KeywordDetected(ISpxKwsEngineAdapter* adapter, uint64_t startOffset, uint32_t size, SpxSharedAudioBuffer_Type audioBuffer) = 0;
    virtual void AdapterCompletedSetFormatStop(ISpxKwsEngineAdapter* adapter) = 0;
};

class ISpxRecoResultFactory : public ISpxInterfaceBaseFor<ISpxRecoResultFactory>
{
public:
    virtual std::shared_ptr<ISpxRecognitionResult> CreateIntermediateResult(const wchar_t* resultId, const wchar_t* text, uint64_t offset, uint64_t duration) = 0;
    virtual std::shared_ptr<ISpxRecognitionResult> CreateFinalResult(const wchar_t* resultId, ResultReason reason, NoMatchReason noMatchReason, CancellationReason cancellation, CancellationErrorCode errorCode, const wchar_t* text, uint64_t offset, uint64_t duration) = 0;
};

class ISpxEventArgsFactory : public ISpxInterfaceBaseFor<ISpxEventArgsFactory>
{
public:
    virtual std::shared_ptr<ISpxSessionEventArgs> CreateSessionEventArgs(const std::wstring& sessionId) = 0;
    virtual std::shared_ptr<ISpxConnectionEventArgs> CreateConnectionEventArgs(const std::wstring& sessionId) = 0;
    virtual std::shared_ptr<ISpxRecognitionEventArgs> CreateRecognitionEventArgs(const std::wstring& sessionId, uint64_t offset) = 0;
    virtual std::shared_ptr<ISpxRecognitionEventArgs> CreateRecognitionEventArgs(const std::wstring& sessionId, std::shared_ptr<ISpxRecognitionResult> result) = 0;
};

class ISpxRecognizerSite : public ISpxInterfaceBaseFor<ISpxRecognizerSite>
{
public:
    virtual std::shared_ptr<ISpxSession> GetDefaultSession() = 0;
};

class ISpxSpeechApiFactory : public ISpxInterfaceBaseFor<ISpxSpeechApiFactory>
{
public:
    virtual std::shared_ptr<ISpxRecognizer> CreateSpeechRecognizerFromConfig(const char* pszLanguage, OutputFormat format, std::shared_ptr<ISpxAudioConfig> audioInput) = 0;
    virtual std::shared_ptr<ISpxRecognizer> CreateIntentRecognizerFromConfig(const char* pszLanguage, OutputFormat format, std::shared_ptr<ISpxAudioConfig> audioInput) = 0;
    virtual std::shared_ptr<ISpxRecognizer> CreateTranslationRecognizerFromConfig(const std::string& sourcelanguage, const std::vector<std::string>& targetLanguages, const std::string& voice, std::shared_ptr<ISpxAudioConfig> audioInput) = 0;
};

class ISpxNamedProperties : public ISpxInterfaceBaseFor<ISpxNamedProperties>
{
public:
    virtual std::string GetStringValue(const char* name, const char* defaultValue = "") const = 0;
    virtual void SetStringValue(const char* name, const char* value) = 0;
    virtual bool HasStringValue(const char* name) const = 0;
    virtual void Copy(ISpxNamedProperties* from) = 0;
};

class ISpxIntentRecognitionResult : public ISpxInterfaceBaseFor<ISpxIntentRecognitionResult>
{
public:
    virtual std::wstring GetIntentId() = 0;
};

class ISpxIntentRecognitionResultInit : public ISpxInterfaceBaseFor<ISpxIntentRecognitionResultInit>
{
public:
    virtual void InitIntentResult(const wchar_t* intentId, const wchar_t* jsonPayload) = 0;
};

enum class TranslationStatusCode { Success, Error };

class ISpxTranslationRecognitionResult : public ISpxInterfaceBaseFor<ISpxTranslationRecognitionResult>
{
public:
    virtual const std::map<std::wstring, std::wstring>& GetTranslationText() = 0;
};

class ISpxTranslationRecognitionResultInit : public ISpxInterfaceBaseFor<ISpxTranslationRecognitionResultInit>
{
public:
    virtual void InitTranslationRecognitionResult(TranslationStatusCode status, const std::map<std::wstring, std::wstring>& translations, const std::wstring& failureReason) = 0;
};

class ISpxTranslationSynthesisResult : public ISpxInterfaceBaseFor<ISpxTranslationSynthesisResult>
{
public:
    virtual const uint8_t* GetAudio() const = 0;
    virtual size_t GetLength() const = 0;
};

class ISpxTranslationSynthesisResultInit : public ISpxInterfaceBaseFor<ISpxTranslationSynthesisResultInit>
{
public:
    virtual void InitTranslationSynthesisResult(const uint8_t* audioData, size_t audioLength) = 0;
};

class ISpxLanguageUnderstandingModel : public ISpxInterfaceBaseFor<ISpxLanguageUnderstandingModel>
{
public:
    virtual void InitAppId(const wchar_t* appId) = 0;
    virtual void InitEndpoint(const wchar_t* uri) = 0;
    virtual void InitSubscription(const wchar_t* subscriptionKey, const wchar_t* appId, const wchar_t* region) = 0;
    virtual void UpdateSubscription(const wchar_t* subscriptionKey, const wchar_t* region) = 0;

    virtual std::wstring GetEndpoint() const = 0;
    virtual std::wstring GetHostName() const = 0;
    virtual std::wstring GetPathAndQuery() const = 0;

    virtual std::wstring GetSubscriptionKey() const = 0;
    virtual std::wstring GetAppId() const = 0;
    virtual std::wstring GetRegion() const = 0;
};

class ISpxTrigger : public ISpxInterfaceBaseFor<ISpxTrigger>
{
public:
    virtual void InitPhraseTrigger(const wchar_t* phrase) = 0;
    virtual void InitLanguageUnderstandingModelTrigger(std::shared_ptr<ISpxLanguageUnderstandingModel> model, const wchar_t* intentName) = 0;

    virtual std::wstring GetPhrase() const = 0;

    virtual std::shared_ptr<ISpxLanguageUnderstandingModel> GetModel() const = 0;
    virtual std::wstring GetModelIntentName() const = 0;
};

class ISpxIntentTriggerService : public ISpxInterfaceBaseFor<ISpxIntentTriggerService>
{
public:
    virtual void AddIntentTrigger(const wchar_t* id, std::shared_ptr<ISpxTrigger> trigger) = 0;

    virtual std::list<std::string> GetListenForList() = 0;
    virtual void GetIntentInfo(std::string& provider, std::string& id, std::string& key, std::string& region) = 0;
};

class ISpxIntentRecognizer : public ISpxInterfaceBaseFor<ISpxIntentRecognizer>
{
public:
    virtual void AddIntentTrigger(const wchar_t* intentId, std::shared_ptr<ISpxTrigger> trigger) = 0;

    // TODO: RobCh: Add additional methods required...
};

class ISpxTranslationRecognizer : public ISpxInterfaceBaseFor<ISpxTranslationRecognizer>
{
};

class ISpxRecognitionResultProcessor : public ISpxInterfaceBaseFor<ISpxRecognitionResultProcessor>
{
public:
    virtual void ProcessResult(std::shared_ptr<ISpxRecognitionResult> result) = 0;
};

class ISpxLuEngineAdapter :
    public ISpxRecognitionResultProcessor,
    public ISpxInterfaceBaseFor<ISpxLuEngineAdapter>
{
};

class ISpxLuEngineAdapterSite : public ISpxInterfaceBaseFor<ISpxLuEngineAdapterSite>
{
};

class ISpxSpeechConfig : public ISpxInterfaceBaseFor<ISpxSpeechConfig>
{
public:
    virtual void InitFromSubscription(const char * subscription, const char* region) = 0;
    virtual void InitFromEndpoint(const char * endpoint, const char* subscription) = 0;
    virtual void InitAuthorizationToken(const char * authToken, const char * region) = 0;
};

class ISpxThreadService : public ISpxInterfaceBaseFor<ISpxThreadService>
{
public:
    using TaskId = int;

    enum class Affinity
    {
        User = 0,
        Background = 1
    };

    // Asynchronously execute a task on a thread. All tasks scheduled with the same affinity using this function
    // are executed in FIFO order.
    //
    // Optional 'executed' promise can be used to be notified about task execution:
    //    true: if the task has been successfully executed
    //    false: if the task has been cancelled
    //    exception: if there was an exception during scheduling
    virtual TaskId ExecuteAsync(std::packaged_task<void()>&& task,
        Affinity affinity = Affinity::Background,
        std::promise<bool>&& executed = std::promise<bool>()) = 0;

    // Asynchronously execute a task on a thread with a delay 'count' number of times.
    //
    // Optional 'executed' promise can be used to be notified about task execution:
    //    true: if the task has been successfully executed
    //    false: if the task has been cancelled
    //    exception: if there was an exception during scheduling
    virtual TaskId ExecuteAsync(std::packaged_task<void()>&& task,
        std::chrono::milliseconds delay,
        Affinity affinity = Affinity::Background,
        std::promise<bool>&& executed = std::promise<bool>()) = 0;

    // Execute a task on a thread synchronously blocking the caller.
    virtual void ExecuteSync(std::packaged_task<void()>&& task,
        Affinity affinity = Affinity::Background) = 0;

    // Cancels the task. If the task is canceled,
    // the corresponding 'canceled' promise is fulfilled.
    virtual bool Cancel(TaskId id) = 0;

    // Cancels all tasks.
    virtual void CancelAllTasks() = 0;
};

} } } } // Microsoft::CognitiveServices::Speech::Impl
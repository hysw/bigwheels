// Copyright 2023 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef ppx_knob_h
#define ppx_knob_h

#include "ppx/command_line_parser.h"
#include "ppx/log.h"

#include <functional>
#include <iostream>
#include <map>
#include <unordered_map>
#include <vector>
#include <array>

namespace ppx {

struct KnobConfig
{
    std::string flagName;
    std::string flagDesc;
    std::string displayName;
};

class KnobManager;

class KnobBase
{
public:
    KnobBase(const KnobConfig& config)
        : mConfig(config) {}

    KnobBase(const KnobBase&)            = delete;
    KnobBase& operator=(const KnobBase&) = delete;

    std::string GetDisplayName() { return mConfig.displayName; }
    std::string GetFlagName() { return mConfig.flagName; }
    std::string GetFlagDesc() { return mConfig.flagDesc; }

protected:
    friend class KnobManager;
    virtual ~KnobBase() = default;
    virtual void Draw() = 0;
    virtual bool ParseOption(const CliOptions::Option&) { return false; }

private:
    KnobConfig mConfig;

    static constexpr size_t kInvalidIndex = std::numeric_limits<size_t>::max();
    // KnobManager fields
    size_t mIndex       = kInvalidIndex;
    size_t mParentIndex = kInvalidIndex;
    // Calculated when needed
    size_t mFirstChild = kInvalidIndex;
    size_t mSibling    = kInvalidIndex;
};

template <typename ValueT>
class Knob : public KnobBase
{
public:
    typedef ValueT ValueType;
    typedef Knob*  PtrType;

    using KnobBase::KnobBase;

    const ValueType& Get() const { return GetValue(); }
    void             Set(const ValueType& v) { return SetValue(v); }

    virtual const ValueType& GetValue() const           = 0;
    virtual void             SetValue(const ValueType&) = 0;

protected:
    virtual ~Knob() = default;
};

namespace knob {
template <typename ValueT>
struct KnobSpecOf
{
    typedef ValueT          ValueType;
    typedef Knob<ValueType> KnobType;
    typedef KnobType*       PtrType;
};

class Checkbox : public KnobSpecOf<bool>
{
    friend class KnobManager;
    static PtrType Create(const KnobConfig&, ValueType);
};

class Combo : public KnobSpecOf<int32_t>
{
    friend class KnobManager;

    static PtrType CreateInternal(const KnobConfig&, ValueType, const std::vector<std::string>&);

    template <typename T>
    static PtrType Create(const KnobConfig& base, ValueType defaultValue, const T& values)
    {
        return CreateInternal(base, defaultValue, std::vector<std::string>(std::begin(values), std::end(values)));
    }
};

class Group : public KnobSpecOf<std::string>
{
    friend class KnobManager;
    static PtrType Create(const KnobConfig& base, const std::string& title);
};

typedef Checkbox::PtrType CheckboxPtr;
typedef Combo::PtrType    ComboPtr;

} // namespace knob

class KnobManager
{
private:
    template <typename KnobT>
    using PtrOf = KnobT::PtrType;

public:
    KnobManager() = default;
    ~KnobManager();

    KnobManager(const KnobManager&)            = delete;
    KnobManager& operator=(const KnobManager&) = delete;

public:
    template <typename KnobT, typename... ArgsT>
    PtrOf<KnobT> Create(const KnobConfig& baseConfig, ArgsT&&... args)
    {
        PtrOf<KnobT> pKnob = KnobT::Create(baseConfig, std::forward<ArgsT>(args)...);
        OnCreate(pKnob);
        return pKnob;
    }

    template <typename KnobT>
    PtrOf<KnobT> GetKnob(const std::string& name)
    {
        return dynamic_cast<PtrOf<KnobT>>(GetKnobInternal(name));
    }

    void DrawAllKnobs(bool inExistingWindow);

    bool        IsEmpty() { return mKnobs.empty(); }
    std::string GetUsageMsg();
    bool        ParseOptions(std::unordered_map<std::string, CliOptions::Option>& optionsMap);

    void SetParent(KnobBase* pChild, KnobBase* pParent)
    {
        InvalidateDrawOrder();
        pChild->mParentIndex = pParent->mIndex;
    }

private:
    std::vector<KnobBase*>                  mKnobs;
    std::unordered_map<std::string, size_t> mNameMap;

    size_t firstKnobToDraw = KnobBase::kInvalidIndex;

    void InvalidateDrawOrder() { firstKnobToDraw = KnobBase::kInvalidIndex; }
    void CalculateDrawOrder();
    void DrawKnobs();

    void      OnCreate(KnobBase* pKnob);
    KnobBase* GetKnobInternal(const std::string& name);
};

} // namespace ppx

#endif // ppx_knob_h

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

#include "backends/imgui_impl_glfw.h"
#include "ppx/knob.h"

#include <cstring>

namespace ppx {

// -------------------------------------------------------------------------------------------------
// KnobManager
// -------------------------------------------------------------------------------------------------

KnobManager::~KnobManager()
{
    for (auto knob : mKnobs) {
        delete knob;
    }
}

void KnobManager::OnCreate(KnobBase* pKnob)
{
    if (!pKnob) {
        return;
    }
    InvalidateDrawOrder();
    size_t knobIndex                      = mKnobs.size();
    static_cast<KnobBase*>(pKnob)->mIndex = knobIndex;
    mKnobs.push_back(pKnob);
    if (!pKnob->GetFlagName().empty()) {
        mNameMap[pKnob->GetFlagName()] = knobIndex;
    }
}

KnobBase* KnobManager::GetKnobInternal(const std::string& name)
{
    if (name.empty()) {
        return nullptr;
    }
    for (auto knob : mKnobs) {
        if (name == knob->GetFlagName()) {
            return knob;
        }
    }
    return nullptr;
}

void KnobManager::DrawAllKnobs(bool inExistingWindow)
{
    if (!inExistingWindow)
        ImGui::Begin("Knobs");
    DrawKnobs();
    if (!inExistingWindow)
        ImGui::End();
}

void KnobManager::CalculateDrawOrder()
{
    if (firstKnobToDraw < mKnobs.size()) {
        return;
    }

    for (size_t i = 0; i < mKnobs.size(); i++) {
        mKnobs[i]->mFirstChild = KnobBase::kInvalidIndex;
        mKnobs[i]->mSibling    = KnobBase::kInvalidIndex;
    }

    for (size_t i = 0; i < mKnobs.size(); i++) {
        size_t  index             = mKnobs.size() - i - 1;
        size_t* pParentFirstChild = &firstKnobToDraw;
        if (mKnobs[index]->mParentIndex < mKnobs.size()) {
            pParentFirstChild = &mKnobs[mKnobs[index]->mParentIndex]->mFirstChild;
        }
        mKnobs[index]->mSibling = *pParentFirstChild;
        *pParentFirstChild      = index;
    }
}

void KnobManager::DrawKnobs()
{
    CalculateDrawOrder();

    size_t at = firstKnobToDraw;
    while (at < mKnobs.size()) {
        mKnobs[at]->Draw();

        if (mKnobs[at]->mFirstChild < mKnobs.size()) {
            ImGui::Indent();
            at = mKnobs[at]->mFirstChild;
            continue;
        }

        while (at < mKnobs.size() && !(mKnobs[at]->mSibling < mKnobs.size())) {
            ImGui::Unindent();
            at = mKnobs[at]->mParentIndex;
        }

        if (at < mKnobs.size()) {
            at = mKnobs[at]->mSibling;
        }
    }
}

std::string KnobManager::GetUsageMsg()
{
    std::string usageMsg = "\nApplication-specific flags\n";
    for (auto pKnob : mKnobs) {
        if (pKnob->GetFlagName().empty()) {
            continue;
        }
        usageMsg += "--" + pKnob->GetFlagName() + ": " + pKnob->GetFlagDesc() + "\n";
    }
    return usageMsg;
}

bool KnobManager::ParseOptions(std::unordered_map<std::string, CliOptions::Option>& optionsMap)
{
    bool allSucceed = true;
    for (auto pair : optionsMap) {
        auto      name    = pair.first;
        auto      opt     = pair.second;
        KnobBase* knobPtr = GetKnobInternal(name);
        if (!knobPtr) {
            continue;
        }
        allSucceed = allSucceed && knobPtr->ParseOption(opt);
    }
    return allSucceed;
}

} // namespace ppx

namespace ppx::knob {

template <typename ValueT>
class KnobValueImpl : public Knob<ValueT>
{
public:
    typedef ValueT ValueType;

    KnobValueImpl(const KnobConfig& base)
        : Knob<ValueType>(base) {}

    KnobValueImpl(const KnobConfig& base, const ValueType& defaultValue)
        : Knob<ValueType>(base), mValue(defaultValue) {}

    const ValueType& GetValue() const override { return mValue; }

    void SetValue(const ValueType& value) override { mValue = value; }

protected:
    ValueType mValue;
};

template <typename KnobT>
class KnobImpl;

// -------------------------------------------------------------------------------------------------
// Checkbox
// -------------------------------------------------------------------------------------------------

template <>
class KnobImpl<Checkbox> : public KnobValueImpl<Checkbox::ValueType>
{
public:
    using KnobValueImpl<Checkbox::ValueType>::KnobValueImpl;
    void Draw() override
    {
        ImGui::Checkbox(GetDisplayName().c_str(), &mValue);
    }
    bool ParseOption(const CliOptions::Option& opt) override
    {
        mValue = opt.GetValueOrDefault<bool>(mValue);
        return true;
    }
};

Checkbox::PtrType Checkbox::Create(const KnobConfig& base, ValueType defaultValue)
{
    return new KnobImpl<Checkbox>(base, defaultValue);
}

// -------------------------------------------------------------------------------------------------
// Combo selector
// -------------------------------------------------------------------------------------------------

template <>
class KnobImpl<Combo> : public KnobValueImpl<Combo::ValueType>
{
public:
    KnobImpl(const KnobConfig& base, ValueType defaultValue, const std::vector<std::string>& values)
        : KnobValueImpl(base, defaultValue)
    {
        std::vector<size_t> offsets;
        for (auto& value : values) {
            offsets.push_back(mStorage.size());
            mStorage.append(value);
            mStorage.push_back('\0');
        }

        const char* pStringTable = mStorage.data();
        for (auto offset : offsets) {
            mValues.push_back(pStringTable + offset);
        }
    }

    void Draw() override
    {
        ImGui::Combo(GetDisplayName().c_str(), &mValue, mValues.data(), static_cast<int>(mValues.size()));
    }

    bool ParseOption(const CliOptions::Option& opt) override
    {
        {
            std::string newValue = opt.GetValueOrDefault<std::string>("");
            auto        iter     = std::find(mValues.begin(), mValues.end(), newValue);
            if (iter != mValues.end()) {
                mValue = static_cast<int32_t>(iter - mValues.begin());
                return true;
            }
        }
        {
            int32_t newValue = opt.GetValueOrDefault<int32_t>(-1);
            if (0 <= newValue && newValue < mValues.size()) {
                mValue = newValue;
                return true;
            }
        }
        return false;
    }

    // not private: this is implementation
    std::vector<const char*> mValues;
    std::string              mStorage;
};

Combo::PtrType Combo::CreateInternal(const KnobConfig& base, ValueType defaultValue, const std::vector<std::string>& values)
{
    return new KnobImpl<Combo>(base, defaultValue, values);
}

// -------------------------------------------------------------------------------------------------
// A group header
// -------------------------------------------------------------------------------------------------
template <>
class KnobImpl<Group> : public KnobValueImpl<Group::ValueType>
{
public:
    using KnobValueImpl<Group::ValueType>::KnobValueImpl;
    void Draw() override
    {
        ImGui::Text(GetValue().c_str());
    }
};

Group::PtrType Group::Create(const KnobConfig& base, const std::string& title)
{
    return new KnobImpl<Group>(base, title);
}

} // namespace ppx::knob
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
#include "ppx/json.h"

#include <cstring>

namespace ppx {

// -------------------------------------------------------------------------------------------------
// KnobManager::GroupNode
// -------------------------------------------------------------------------------------------------

class KnobManager::GroupNode : public KnobManager::Node
{
protected:
    virtual void Draw() override;

private:
    std::string mTitle;

    friend class KnobManager;
    GroupNode(const std::string&);
};

KnobManager::GroupNode::GroupNode(const std::string& title)
    : mTitle(title)
{
}

void KnobManager::GroupNode::Draw()
{
    ImGui::Text("%s", mTitle.c_str());
}

// -------------------------------------------------------------------------------------------------
// KnobManager
// -------------------------------------------------------------------------------------------------

KnobManager::~KnobManager()
{
    for (auto node : mNodes) {
        delete node;
    }
    mNodes.clear();
    mKnobs.clear();
}

void KnobManager::AddChild(Node* pParent, Node* pChild)
{
    if (pChild == nullptr) {
        return;
    }
    Node::List* pChildrenList = &mRoots;
    if (pParent) {
        pChildrenList = &pParent->mChildren;
    }
    pChild->mParent = pParent;
    pChildrenList->Append(pChild);
}

KnobManager::GroupRef KnobManager::CreateGroupInternal(Node* pParent, const std::string& title)
{
    GroupNode* pNode = new GroupNode(title);
    OnCreateNode(pParent, pNode);
    return GroupRef(this, pNode);
}

void KnobManager::OnCreateNode(Node* pParent, Node* pNode)
{
    if (!pNode) {
        return;
    }
    mNodes.push_back(pNode);
    AddChild(pParent, pNode);
}

void KnobManager::OnCreateKnob(Node* pParent, KnobManager::Knob* pKnob)
{
    if (!pKnob) {
        return;
    }
    OnCreateNode(pParent, pKnob);

    mKnobs.push_back(pKnob);
    if (!pKnob->GetFlagName().empty()) {
        mNameMap[pKnob->GetFlagName()] = pKnob;
    }
}

KnobManager::Knob* KnobManager::GetKnobInternal(const std::string& name)
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

void KnobManager::DrawKnobs()
{
    Node* at = mRoots.pBegin;
    while (at) {
        at->Draw();
        if (at->mChildren.pBegin) {
            ImGui::Indent();
            at = at->mChildren.pBegin;
            continue;
        }

        while (at && !at->mSibling) {
            ImGui::Unindent();
            at = at->mParent;
        }

        if (at) {
            at = at->mSibling;
        }
    }
}

std::string KnobManager::GetUsageMsg()
{
    std::string usageMsg = "\nApplication-specific flags\n";
    for (auto pKnob : mKnobs) {
        usageMsg += "--" + pKnob->GetFlagName() + ": " + pKnob->GetFlagDesc() + "\n";
    }
    return usageMsg;
}

bool KnobManager::ParseOptions(std::unordered_map<std::string, CliOptions::Option>& optionsMap)
{
    bool allSucceed = true;
    for (auto pair : optionsMap) {
        auto  name    = pair.first;
        auto  opt     = pair.second;
        Knob* knobPtr = GetKnobInternal(name);
        if (!knobPtr) {
            continue;
        }
        allSucceed = allSucceed && knobPtr->ParseOption(opt);
    }
    return allSucceed;
}

std::string KnobManager::SerializeJsonOptions()
{
    json j;
    for (auto pKnob : mKnobs) {
        pKnob->Serialize(&j[pKnob->GetFlagName()]);
    }
    return j.dump();
}

void KnobManager::ParseJsonOptions(const std::string& str)
{
    const json j = json::parse(str);
    for (auto pKnob : mKnobs) {
        if (!j.contains(pKnob->GetFlagName())) {
            continue;
        }
        pKnob->Deserialize(&j[pKnob->GetFlagName()]);
    }
}

} // namespace ppx

namespace ppx::knob {

template <typename KnobSpecT>
class KnobValueImpl : public Knob<KnobSpecT>
{
public:
    typedef typename KnobSpecT::ValueType ValueType;

    KnobValueImpl(const std::string& base)
        : Knob<KnobSpecT>(base) {}

    KnobValueImpl(const std::string& base, const ValueType& defaultValue)
        : Knob<KnobSpecT>(base), mValue(defaultValue) {}

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
class KnobImpl<Checkbox> : public KnobValueImpl<Checkbox>
{
public:
    using KnobValueImpl<Checkbox>::KnobValueImpl;

    void Draw() override
    {
        ImGui::Checkbox(GetDisplayName().c_str(), &mValue);
    }
    bool ParseOption(const CliOptions::Option& opt) override
    {
        mValue = opt.GetValueOrDefault<bool>(mValue);
        return true;
    }
    void Serialize(json* pData) override
    {
        *pData = mValue;
    }
    bool Deserialize(const json* pData) override
    {
        if (!pData->is_boolean()) {
            return false;
        }
        mValue = pData->get<bool>();
        return true;
    }
};

Checkbox::PtrType Checkbox::Create(const std::string& base, ValueType defaultValue)
{
    return new KnobImpl<Checkbox>(base, defaultValue);
}

// -------------------------------------------------------------------------------------------------
// Combo selector
// -------------------------------------------------------------------------------------------------

template <>
class KnobImpl<Combo> : public KnobValueImpl<Combo>
{
public:
    KnobImpl(const std::string& base, ValueType defaultValue, const std::vector<std::string>& values)
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

    bool FromString(const std::string& newValue)
    {
        auto iter = std::find(mValues.begin(), mValues.end(), newValue);
        if (iter == mValues.end()) {
            return false;
        }
        mValue = static_cast<int32_t>(iter - mValues.begin());
        return true;
    }

    bool ParseOption(const CliOptions::Option& opt) override
    {
        {
            std::string newValue = opt.GetValueOrDefault<std::string>("");
            if (FromString(newValue)) {
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

    void Serialize(json* pData) override
    {
        *pData = mValue;
    }

    bool Deserialize(const json* pData) override
    {
        if (!pData->is_string()) {
            return false;
        }
        return FromString(pData->get<std::string>());
    }
    // not private: this is implementation
    std::vector<const char*> mValues;
    std::string              mStorage;
};

Combo::PtrType Combo::CreateInternal(const std::string& base, ValueType defaultValue, const std::vector<std::string>& values)
{
    return new KnobImpl<Combo>(base, defaultValue, values);
}

} // namespace ppx::knob

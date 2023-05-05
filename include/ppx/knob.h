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
#include "ppx/json_fwd.h"

#include <functional>
#include <iostream>
#include <map>
#include <unordered_map>
#include <vector>
#include <array>

namespace ppx {

class KnobManager
{
private:
    template <typename KnobT>
    using PtrOf = KnobT::PtrType;

public:
    class Node
    {
    protected:
        Node()          = default;
        virtual ~Node() = default;

    protected:
        virtual void Draw() = 0;

    private:
        friend class KnobManager;

        static constexpr size_t kInvalidIndex = std::numeric_limits<size_t>::max();
        // KnobManager fields
        Node* mParent  = nullptr;
        Node* mSibling = nullptr;
        struct List
        {
            Node*  pBegin = nullptr;
            Node** ppEnd  = &pBegin;
            void   Append(Node* node)
            {
                *ppEnd = node;
                ppEnd  = &node->mSibling;
            }
        } mChildren;
    };
    
    class Knob : public Node
    {
    public:
        virtual std::string GetFlagName() const = 0;
        virtual std::string GetFlagDesc() const = 0;

    protected:
        friend class KnobManager;
        virtual bool ParseOption(const CliOptions::Option&) = 0;
        virtual void Serialize(json*) = 0;
        virtual bool Deserialize(const json*) = 0;
    };

    class GroupRef
    {
    public:
        template <typename KnobT, typename... ArgsT>
        PtrOf<KnobT> Create(ArgsT&&... args)
        {
            return mManager->CreateInternal<KnobT, ArgsT...>(mNode, std::forward<ArgsT>(args)...);
        }

        GroupRef CreateGroup(const std::string& title)
        {
            return mManager->CreateGroupInternal(mNode, title);
        }

    private:
        friend class KnobManager;
        GroupRef(KnobManager* pManager, Node* pNode)
            : mManager(pManager), mNode(pNode) {}
        KnobManager* mManager;
        Node*        mNode;
    };

public:
    KnobManager() = default;
    ~KnobManager();

    KnobManager(const KnobManager&)            = delete;
    KnobManager& operator=(const KnobManager&) = delete;

public:
    template <typename KnobT, typename... ArgsT>
    PtrOf<KnobT> Create(ArgsT&&... args)
    {
        return CreateInternal<KnobT, ArgsT...>(nullptr, std::forward<ArgsT>(args)...);
    }

    GroupRef CreateGroup(const std::string& title)
    {
        return CreateGroupInternal(nullptr, title);
    }

    template <typename KnobT>
    PtrOf<KnobT> GetKnob(const std::string& name)
    {
        return dynamic_cast<PtrOf<KnobT>>(GetKnobInternal(name));
    }

    void DrawAllKnobs(bool inExistingWindow);

    std::string SerializeJsonOptions();
    void ParseJsonOptions(const std::string&);

    bool        IsEmpty() { return mKnobs.empty(); }
    std::string GetUsageMsg();
    bool        ParseOptions(std::unordered_map<std::string, CliOptions::Option>& optionsMap);

private:
    std::vector<Node*> mNodes;
    std::vector<Knob*> mKnobs;

    std::unordered_map<std::string, Knob*> mNameMap;

    Node::List mRoots;

    void DrawKnobs();

    void AddChild(Node* pParent, Node* pChild);

    template <typename KnobT, typename... ArgsT>
    PtrOf<KnobT> CreateInternal(Node* pParent, ArgsT&&... args)
    {
        PtrOf<KnobT> pKnob = KnobT::Create(std::forward<ArgsT>(args)...);
        OnCreateKnob(pParent, pKnob);
        return pKnob;
    }

    class GroupNode;

    void     OnCreateNode(Node* pParent, Node* pNode);
    void     OnCreateKnob(Node* pParent, Knob* pKnob);
    Knob*    GetKnobInternal(const std::string& name);
    GroupRef CreateGroupInternal(Node* pParent, const std::string& title);
};

template <typename ValueT>
class Knob : public KnobManager::Knob
{
public:
    typedef ValueT ValueType;
    typedef Knob*  PtrType;

    Knob(const std::string& name)
        : mFlagName(name), mDisplayName(name) {}

    const ValueType& Get() const { return GetValue(); }
    void             Set(const ValueType& v) { return SetValue(v); }

    virtual const ValueType& GetValue() const           = 0;
    virtual void             SetValue(const ValueType&) = 0;

    std::string GetFlagName() const override { return mFlagName; }
    std::string GetFlagDesc() const override { return mFlagDesc; }

    void SetFlagName(const std::string& flagName) { mFlagName = flagName; }
    void SetFlagDesc(const std::string& flagDesc) { mFlagDesc = flagDesc; }

    std::string GetDisplayName() const { return mDisplayName; }

    void SetDisplayName(const std::string& displayName) { mDisplayName = displayName; }

private:
    std::string mFlagName;
    std::string mFlagDesc;
    std::string mDisplayName;
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
    static PtrType Create(const std::string&, ValueType);
};

class Combo : public KnobSpecOf<int32_t>
{
    friend class KnobManager;

    static PtrType CreateInternal(const std::string&, ValueType, const std::vector<std::string>&);

    template <typename T>
    static PtrType Create(const std::string& name, ValueType defaultValue, const T& values)
    {
        return CreateInternal(name, defaultValue, std::vector<std::string>(std::begin(values), std::end(values)));
    }
};

typedef Checkbox::PtrType CheckboxPtr;
typedef Combo::PtrType    ComboPtr;

} // namespace knob

} // namespace ppx

#endif // ppx_knob_h

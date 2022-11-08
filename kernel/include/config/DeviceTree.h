#pragma once

#include <stdint.h>
#include <Optional.h>

namespace Npk::Config
{
    using DtbPtr = size_t;

    struct DtNode;
    
    struct DtProperty
    {
        DtbPtr ptr;
        const char* name;

        const char* ReadStr(size_t index = 0) const;
        uint32_t ReadNumber() const;
        size_t ReadRegs(const DtNode& parent, uintptr_t* bases, size_t* lengths) const;
        size_t ReadPairs(size_t aCells, size_t bCells, size_t* aStore, size_t* bStore) const;

    };

    struct DtNode
    {
        DtbPtr ptr;
        DtbPtr childPtr;
        DtbPtr propPtr;
        uint16_t propCount;
        uint16_t childCount;
        uint8_t addrCells;
        uint8_t sizeCells;
        uint8_t childAddrCells;
        uint8_t childSizeCells;
        const char* name;

        sl::Opt<const DtProperty> GetProp(size_t index) const;
        sl::Opt<const DtProperty> GetProp(const char* name) const;
    };

    class DeviceTree
    {
    private:
        const uint32_t* cells;
        size_t cellsCount = 0; //used to store init state, if this is non-zero, its been initialized.
        const uint8_t* strings;
        DtNode rootNode;

        sl::Opt<const DtNode> FindCompatibleHelper(const DtNode& scan, const char* str, size_t strlen) const;
        size_t SkipNode(size_t start) const;
        DtNode CreateNode(size_t start, size_t addrCells, size_t sizeCells) const;
        DtProperty CreateProperty(size_t start) const;

    public:
        static DeviceTree& Global();
        
        void Init(uintptr_t dtbAddr);

        sl::Opt<const DtNode> GetNode(const char* path) const;
        sl::Opt<const DtNode> GetCompatibleNode(const char* compatStr) const;
        sl::Opt<const DtNode> GetChild(const DtNode& parent, const char* name) const;
        sl::Opt<const DtNode> GetChild(const DtNode& parent, size_t index) const;
        sl::Opt<const DtProperty> GetProp(const DtNode& node, const char*) const;
        sl::Opt<const DtProperty> GetProp(const DtNode& node, size_t index) const;

        const char* ReadStr(const DtProperty& prop, size_t index = 0) const;
        uint32_t ReadNumber(const DtProperty& prop) const;
        size_t ReadRegs(const DtNode& node, const DtProperty& prop, uintptr_t* bases, size_t* lengths) const;
        size_t ReadPairs(const DtProperty& prop, size_t aCells, size_t bCells, size_t* aStore, size_t* bStore) const;
    };

    //effectively these are just type-safe macros.

    [[gnu::always_inline]]
    inline const char* DtProperty::ReadStr(size_t index) const
    { return DeviceTree::Global().ReadStr(*this, index); }

    [[gnu::always_inline]]
    inline uint32_t DtProperty::ReadNumber() const
    { return DeviceTree::Global().ReadNumber(*this); }

    [[gnu::always_inline]]
    inline size_t DtProperty::ReadRegs(const DtNode& parent, uintptr_t* bases, size_t* lengths) const
    { return DeviceTree::Global().ReadRegs(parent, *this, bases, lengths); }

    [[gnu::always_inline]]
    inline size_t DtProperty::ReadPairs(size_t aCells, size_t bCells, size_t* aStore, size_t* bStore) const
    { return DeviceTree::Global().ReadPairs(*this, aCells, bCells, aStore, bStore); }

    [[gnu::always_inline]]
    inline sl::Opt<const DtProperty> DtNode::GetProp(size_t index) const
    { return DeviceTree::Global().GetProp(*this, index); }

    [[gnu::always_inline]]
    inline sl::Opt<const DtProperty> DtNode::GetProp(const char* name) const
    { return DeviceTree::Global().GetProp(*this, name); }
}
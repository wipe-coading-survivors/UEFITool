/* uefiedit.cpp

Copyright (c) 2026, LongSoft. All rights reserved.
This program and the accompanying materials
are licensed and made available under the terms and conditions of the BSD License
which accompanies this distribution.  The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php

THE PROGRAM IS DISTRIBUTED ON THE BSD LICENSE ON AN "AS IS" BASIS,
WITHWARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.
*/

#include "uefiedit.h"
#include <iostream>
#include <fstream>
#include <cstring>
#include <sstream>

UEFIEdit::UEFIEdit()
{
    model = new TreeModel();
    ffsParser = new FfsParser(model);
    ffsOps = new FfsOperations(model);
    ffsBuilder = new FfsBuilder(model);
    initDone = false;
}

UEFIEdit::~UEFIEdit()
{
    delete ffsBuilder;
    delete ffsOps;
    delete ffsParser;
    delete model;
}

USTATUS UEFIEdit::init(const UString & imagePath)
{
    if (false == readFileIntoBuffer(imagePath, originalBuffer))
        return U_FILE_OPEN;

    USTATUS result = ffsParser->parse(originalBuffer);
    if (result)
        return result;

    initDone = true;
    return U_SUCCESS;
}

USTATUS UEFIEdit::save(const UString & outputPath)
{
    if (!initDone)
        return U_INVALID_PARAMETER;

    UModelIndex root = model->index(0, 0);
    if (!root.isValid())
        return U_INVALID_PARAMETER;

    ffsBuilder->clearMessages();
    UByteArray image;
    USTATUS result = ffsBuilder->build(root, image);
    if (result) {
        std::cerr << "Build failed: " << errorCodeToUString(result).toLocal8Bit() << std::endl;
        for (auto &m : ffsBuilder->getMessages())
            std::cerr << "  BUILDER: " << m.first.toLocal8Bit() << std::endl;
        return result;
    }

    std::ofstream out(outputPath.toLocal8Bit(), std::ios::binary);
    if (!out) {
        std::cerr << "Cannot open " << outputPath.toLocal8Bit() << " for writing" << std::endl;
        return U_FILE_WRITE;
    }
    out.write(image.constData(), image.size());
    out.close();
    return U_SUCCESS;
}

// Parse a GUID string of the form "XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX"
// (case-insensitive, dashes optional) into an EFI_GUID.
static bool parseGuidString(const UString & s, EFI_GUID & guid)
{
    std::string normalized;
    const char *p = s.toLocal8Bit();
    for (; *p; ++p) {
        char c = *p;
        if (c == '-' || c == ' ' || c == '{' || c == '}')
            continue;
        if (c >= 'A' && c <= 'F')
            c = (char)(c - 'A' + 'a');
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
            return false;
        normalized += c;
    }
    if (normalized.size() != 32)
        return false;

    auto hexByte = [](const char *h) -> UINT8 {
        auto v = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            return 0;
        };
        return (UINT8)((v(h[0]) << 4) | v(h[1]));
    };
    auto hexWord = [&](const char *h) -> UINT16 {
        return (UINT16)((hexByte(h) << 8) | hexByte(h + 2));
    };
    auto hexDword = [&](const char *h) -> UINT32 {
        return (UINT32)(((UINT32)hexWord(h) << 16) | hexWord(h + 4));
    };

    const char *h = normalized.c_str();
    guid.Data1 = hexDword(h);       h += 8;
    guid.Data2 = hexWord(h);        h += 4;
    guid.Data3 = hexWord(h);        h += 4;
    for (int i = 0; i < 8; ++i) {
        guid.Data4[i] = hexByte(h);
        h += 2;
    }
    return true;
}

UModelIndex UEFIEdit::findItemByGuidRecursive(const UModelIndex & parent, const EFI_GUID & guid)
{
    if (!parent.isValid())
        return UModelIndex();

    if (model->type(parent) == Types::File && !model->hasEmptyHeader(parent)) {
        UByteArray hdr = model->header(parent);
        if ((UINT32)hdr.size() >= sizeof(EFI_GUID)) {
            const EFI_GUID *itemGuid = (const EFI_GUID *)hdr.constData();
            if (std::memcmp(itemGuid, &guid, sizeof(EFI_GUID)) == 0)
                return parent;
        }
    }

    if (model->type(parent) == Types::Volume && !model->hasEmptyParsingData(parent)) {
        UByteArray pdata = model->parsingData(parent);
        if ((UINT32)pdata.size() >= sizeof(VOLUME_PARSING_DATA)) {
            const VOLUME_PARSING_DATA *vpd = (const VOLUME_PARSING_DATA *)pdata.constData();
            if (vpd->hasExtendedHeader && std::memcmp(&vpd->extendedHeaderGuid, &guid, sizeof(EFI_GUID)) == 0)
                return parent;
        }
    }

    if (model->type(parent) == Types::Section && !model->hasEmptyParsingData(parent)) {
        UByteArray pdata = model->parsingData(parent);
        if ((UINT32)pdata.size() >= sizeof(EFI_GUID)) {
            const EFI_GUID *itemGuid = (const EFI_GUID *)pdata.constData();
            if (std::memcmp(itemGuid, &guid, sizeof(EFI_GUID)) == 0)
                return parent;
        }
    }

    for (int i = 0; i < model->rowCount(parent); ++i) {
        UModelIndex found = findItemByGuidRecursive(model->index(i, 0, parent), guid);
        if (found.isValid())
            return found;
    }
    return UModelIndex();
}

UModelIndex UEFIEdit::findItemByGuid(const UString & guidStr)
{
    EFI_GUID guid;
    if (!parseGuidString(guidStr, guid))
        return UModelIndex();
    return findItemByGuidRecursive(model->index(0, 0), guid);
}

UModelIndex UEFIEdit::findSectionByTypeRecursive(const UModelIndex & parent, const UINT8 sectionType, int & index)
{
    if (!parent.isValid())
        return UModelIndex();

    if (model->type(parent) == Types::Section && model->subtype(parent) == sectionType) {
        if (index == 0)
            return parent;
        --index;
    }

    for (int i = 0; i < model->rowCount(parent); ++i) {
        UModelIndex found = findSectionByTypeRecursive(model->index(i, 0, parent), sectionType, index);
        if (found.isValid())
            return found;
    }
    return UModelIndex();
}

// Parse a target string into a Target descriptor.
//   "GUID"                       -> Guid
//   "0/2/207"                    -> Path (decimal child indices from root)
//   "GUID:0x10"                  -> GuidSection (file GUID + section type, 0-based default)
//   "GUID:0x10:2"               -> GuidSection with explicit occurrence index
bool UEFIEdit::parseTarget(const UString & s, Target & t)
{
    std::string str = s.toLocal8Bit();
    if (str.empty())
        return false;

    // Path form: starts with a digit and contains only digits and '/'.
    bool allDigitsAndSlashes = true;
    for (char c : str) {
        if (!((c >= '0' && c <= '9') || c == '/')) {
            allDigitsAndSlashes = false;
            break;
        }
    }
    if (str[0] >= '0' && str[0] <= '9' && allDigitsAndSlashes && str.find('/') != std::string::npos) {
        t.kind = Target::Path;
        t.path.clear();
        std::stringstream ss(str);
        std::string item;
        while (std::getline(ss, item, '/')) {
            if (item.empty()) continue;
            t.path.push_back(std::stoi(item));
        }
        return !t.path.empty();
    }

    // GUID:sectionType[:index] form: contains ':' after a GUID.
    size_t colon = str.find(':');
    if (colon != std::string::npos && colon >= 32) {
        std::string guidPart = str.substr(0, colon);
        std::string rest = str.substr(colon + 1);
        if (!parseGuidString(UString(guidPart.c_str()), t.guid))
            return false;
        std::string secTypeStr, indexStr;
        size_t colon2 = rest.find(':');
        if (colon2 != std::string::npos) {
            secTypeStr = rest.substr(0, colon2);
            indexStr = rest.substr(colon2 + 1);
        }
        else {
            secTypeStr = rest;
            indexStr = "";
        }
        unsigned int st = 0;
        std::stringstream ss;
        if (secTypeStr.size() > 2 && secTypeStr[0] == '0' && (secTypeStr[1] == 'x' || secTypeStr[1] == 'X'))
            ss << std::hex << secTypeStr.substr(2);
        else
            ss << std::hex << secTypeStr;
        ss >> st;
        t.sectionType = (UINT8)st;
        int idx = 0;
        if (!indexStr.empty())
            idx = std::stoi(indexStr);
        t.sectionIndex = idx < 0 ? 0 : idx;
        t.kind = Target::GuidSection;
        return true;
    }

    // Plain GUID form.
    if (parseGuidString(s, t.guid)) {
        t.kind = Target::Guid;
        return true;
    }

    return false;
}

UModelIndex UEFIEdit::resolveTarget(const Target & t)
{
    if (t.kind == Target::Guid) {
        return findItemByGuidRecursive(model->index(0, 0), t.guid);
    }

    if (t.kind == Target::Path) {
        // The path format mirrors "dump"/"list" output: the first element is the
        // root row (always 0), each subsequent element descends into a child.
        if (t.path.empty())
            return UModelIndex();
        UModelIndex current = model->index(0, 0);
        for (size_t i = 1; i < t.path.size(); ++i) {
            int row = t.path[i];
            if (row < 0 || row >= model->rowCount(current))
                return UModelIndex();
            current = model->index(row, 0, current);
        }
        return current;
    }

    if (t.kind == Target::GuidSection) {
        UModelIndex fileIndex = findItemByGuidRecursive(model->index(0, 0), t.guid);
        if (!fileIndex.isValid())
            return UModelIndex();
        int index = t.sectionIndex;
        return findSectionByTypeRecursive(fileIndex, t.sectionType, index);
    }

    return UModelIndex();
}

UModelIndex UEFIEdit::findItem(const UString & target)
{
    Target t;
    if (!parseTarget(target, t)) {
        std::cerr << "Invalid target: " << target.toLocal8Bit() << std::endl;
        return UModelIndex();
    }
    UModelIndex idx = resolveTarget(t);
    if (!idx.isValid())
        std::cerr << "Item '" << target.toLocal8Bit() << "' not found" << std::endl;
    return idx;
}

// Read the data file and split into header+body depending on the target.
// For File insertion: parse EFI_FFS_FILE_HEADER (with large-file detection).
// For Section insertion: parse EFI_COMMON_SECTION_HEADER (with section2 detection).
static bool readAndSplit(const UString & path, bool isFile, const UModelIndex & parentVolume,
                         TreeModel * model, UINT8 & outType, UINT8 & outSubtype,
                         UByteArray & header, UByteArray & body)
{
    UByteArray data;
    if (!readFileIntoBuffer(path, data))
        return false;

    if (isFile) {
        if ((UINT32)data.size() < sizeof(EFI_FFS_FILE_HEADER))
            return false;
        const EFI_FFS_FILE_HEADER *fh = (const EFI_FFS_FILE_HEADER *)data.constData();
        UINT32 headerSize = sizeof(EFI_FFS_FILE_HEADER);
        if (fh->Attributes & FFS_ATTRIB_LARGE_FILE) {
            UINT8 ffsVersion = 2, revision = 2;
            if (parentVolume.isValid() && !model->hasEmptyParsingData(parentVolume)) {
                VOLUME_PARSING_DATA pdata = *(const VOLUME_PARSING_DATA *)model->parsingData(parentVolume).constData();
                ffsVersion = pdata.ffsVersion;
                revision = pdata.revision;
            }
            if (ffsVersion == 2 && revision == 2)
                headerSize = sizeof(EFI_FFS_FILE_HEADER2_LENOVO);
            else if (ffsVersion == 3)
                headerSize = sizeof(EFI_FFS_FILE_HEADER2);
        }
        outType = Types::File;
        outSubtype = fh->Type;
        header = UByteArray(data.constData(), headerSize);
        body = UByteArray(data.constData() + headerSize, data.size() - headerSize);
        return true;
    }
    else {
        if ((UINT32)data.size() < sizeof(EFI_COMMON_SECTION_HEADER))
            return false;
        const EFI_COMMON_SECTION_HEADER *sh = (const EFI_COMMON_SECTION_HEADER *)data.constData();
        UINT32 headerSize = sizeof(EFI_COMMON_SECTION_HEADER);
        UINT8 ffsVersion = 2;
        if (parentVolume.isValid() && !model->hasEmptyParsingData(parentVolume)) {
            VOLUME_PARSING_DATA pdata = *(const VOLUME_PARSING_DATA *)model->parsingData(parentVolume).constData();
            ffsVersion = pdata.ffsVersion;
        }
        if (ffsVersion == 3 && uint24ToUint32(sh->Size) == EFI_SECTION2_IS_USED) {
            if ((UINT32)data.size() < sizeof(EFI_COMMON_SECTION_HEADER2))
                return false;
            const EFI_COMMON_SECTION_HEADER2 *sh2 = (const EFI_COMMON_SECTION_HEADER2 *)data.constData();
            headerSize = sizeof(EFI_COMMON_SECTION_HEADER2);
            UINT32 fullSize = sh2->ExtendedSize;
            if (fullSize > (UINT32)data.size())
                fullSize = (UINT32)data.size();
            body = UByteArray(data.constData() + headerSize, fullSize - headerSize);
        }
        else {
            UINT32 fullSize = uint24ToUint32(sh->Size);
            if (fullSize > (UINT32)data.size())
                fullSize = (UINT32)data.size();
            body = UByteArray(data.constData() + headerSize, fullSize - headerSize);
        }
        outType = Types::Section;
        outSubtype = sh->Type;
        header = UByteArray(data.constData(), headerSize);
        return true;
    }
}

USTATUS UEFIEdit::insert(const UString & target, const UINT8 mode, const UString & dataPath)
{
    if (!initDone)
        return U_INVALID_PARAMETER;

    UModelIndex index = findItem(target);
    if (!index.isValid())
        return U_ITEM_NOT_FOUND;

    UModelIndex parentIndex;
    UModelIndex refItem;
    UINT8 createMode;
    if (mode == CREATE_MODE_PREPEND) {
        parentIndex = index;
        refItem = index;
        createMode = CREATE_MODE_PREPEND;
    }
    else if (mode == CREATE_MODE_BEFORE || mode == CREATE_MODE_AFTER) {
        parentIndex = index.parent();
        refItem = index;
        createMode = mode;
    }
    else {
        return U_INVALID_PARAMETER;
    }
    if (!parentIndex.isValid())
        return U_INVALID_PARAMETER;

    UINT8 parentType = model->type(parentIndex);
    bool insertFile = false;
    if (parentType == Types::Volume)
        insertFile = true;
    else if (parentType == Types::File)
        insertFile = false;
    else if (parentType == Types::Section) {
        UINT8 parentSubtype = model->subtype(parentIndex);
        if (parentSubtype != EFI_SECTION_COMPRESSION
            && parentSubtype != EFI_SECTION_GUID_DEFINED
            && parentSubtype != EFI_SECTION_DISPOSABLE) {
            std::cerr << "Can only insert into encapsulation sections" << std::endl;
            return U_INVALID_PARAMETER;
        }
        insertFile = false;
    }
    else {
        std::cerr << "Cannot insert into type " << (int)parentType << std::endl;
        return U_INVALID_PARAMETER;
    }

    UModelIndex parentVolume = (parentType == Types::Volume) ? parentIndex : model->findParentOfType(parentIndex, Types::Volume);
    UINT8 newType, newSubtype;
    UByteArray header, body;
    if (!readAndSplit(dataPath, insertFile, parentVolume, model, newType, newSubtype, header, body)) {
        std::cerr << "Failed to read/parse " << dataPath.toLocal8Bit() << std::endl;
        return U_INVALID_PARAMETER;
    }

    UString name;
    if (newType == Types::File) {
        const EFI_FFS_FILE_HEADER *fh = (const EFI_FFS_FILE_HEADER *)header.constData();
        name = guidToUString(fh->Name);
    }
    else {
        name = sectionTypeToUString(newSubtype) + UString(" section");
    }

    UModelIndex newIndex = model->addItem(model->offset(index), newType, newSubtype,
                                          name, UString(), UString(),
                                          header, body, UByteArray(),
                                          Movable, refItem, createMode);
    if (!newIndex.isValid()) {
        std::cerr << "Failed to add item to the tree" << std::endl;
        return U_INVALID_PARAMETER;
    }

    if (newType == Types::File) {
        const EFI_FFS_FILE_HEADER *fh = (const EFI_FFS_FILE_HEADER *)header.constData();
        FILE_PARSING_DATA pdata = {};
        pdata.emptyByte = (fh->State & EFI_FILE_ERASE_POLARITY) ? 0xFF : 0x00;
        pdata.guid = fh->Name;
        model->setParsingData(newIndex, UByteArray((const char *)&pdata, sizeof(pdata)));
    }

    model->setAction(newIndex, Actions::Insert);
    for (UModelIndex p = parentIndex; p.isValid() && model->type(p) != Types::Root; p = p.parent()) {
        if (model->action(p) == Actions::NoAction)
            model->setAction(p, Actions::Rebuild);
    }
    UModelIndex root = model->index(0, 0);
    if (root.isValid() && model->action(root) == Actions::NoAction)
        model->setAction(root, Actions::Rebuild);

    std::cerr << "Inserted " << (insertFile ? "file" : "section") << " '" << name.toLocal8Bit()
              << "' (type=" << (int)newType << " subtype=" << (int)newSubtype << ")" << std::endl;
    return U_SUCCESS;
}

USTATUS UEFIEdit::remove(const UString & target)
{
    if (!initDone)
        return U_INVALID_PARAMETER;

    UModelIndex index = findItem(target);
    if (!index.isValid())
        return U_ITEM_NOT_FOUND;

    USTATUS result = ffsOps->remove(index);
    if (result)
        return result;
    for (UModelIndex p = index.parent(); p.isValid() && model->type(p) != Types::Root; p = p.parent()) {
        if (model->action(p) == Actions::NoAction)
            model->setAction(p, Actions::Rebuild);
    }
    UModelIndex root = model->index(0, 0);
    if (root.isValid() && model->action(root) == Actions::NoAction)
        model->setAction(root, Actions::Rebuild);
    return U_SUCCESS;
}

USTATUS UEFIEdit::replace(const UString & target, const UINT8 mode, const UString & dataPath)
{
    if (!initDone)
        return U_INVALID_PARAMETER;

    UModelIndex index = findItem(target);
    if (!index.isValid())
        return U_ITEM_NOT_FOUND;

    UByteArray data;
    if (!readFileIntoBuffer(dataPath, data))
        return U_FILE_OPEN;

    USTATUS result = ffsOps->replace(index, data, mode);
    if (result)
        return result;
    for (UModelIndex p = index.parent(); p.isValid() && model->type(p) != Types::Root; p = p.parent()) {
        if (model->action(p) == Actions::NoAction)
            model->setAction(p, Actions::Rebuild);
    }
    UModelIndex root = model->index(0, 0);
    if (root.isValid() && model->action(root) == Actions::NoAction)
        model->setAction(root, Actions::Rebuild);
    return U_SUCCESS;
}

USTATUS UEFIEdit::rebuild(const UString & target)
{
    if (!initDone)
        return U_INVALID_PARAMETER;

    UModelIndex index = findItem(target);
    if (!index.isValid())
        return U_ITEM_NOT_FOUND;

    USTATUS result = ffsOps->rebuild(index);
    if (result)
        return result;
    for (UModelIndex p = index.parent(); p.isValid() && model->type(p) != Types::Root; p = p.parent()) {
        if (model->action(p) == Actions::NoAction)
            model->setAction(p, Actions::Rebuild);
    }
    UModelIndex root = model->index(0, 0);
    if (root.isValid() && model->action(root) == Actions::NoAction)
        model->setAction(root, Actions::Rebuild);
    return U_SUCCESS;
}

static const char *typeName(UINT8 t)
{
    switch (t) {
    case Types::Capsule:  return "Capsule";
    case Types::Image:    return "Image";
    case Types::Region:   return "Region";
    case Types::Padding:  return "Padding";
    case Types::Volume:   return "Volume";
    case Types::File:      return "File";
    case Types::Section:   return "Section";
    case Types::FreeSpace: return "FreeSpace";
    default:               return "?";
    }
}

static void dumpTreeRecursive(TreeModel * model, const UModelIndex & parent,
                               const std::string & path, int row)
{
    if (!parent.isValid())
        return;
    std::string curPath = path.empty() ? std::to_string(row) : path + "/" + std::to_string(row);
    UINT8 t = model->type(parent);
    UINT8 st = model->subtype(parent);
    std::cout << curPath
              << " type=" << (int)t << " (" << typeName(t) << ")"
              << " subtype=" << (int)st
              << " name='" << model->name(parent).toLocal8Bit() << "'"
              << " action=" << (int)model->action(parent)
              << " hdr=" << model->headerSize(parent)
              << " body=" << model->bodySize(parent)
              << " tail=" << model->tailSize(parent)
              << " children=" << model->rowCount(parent)
              << std::endl;
    for (int i = 0; i < model->rowCount(parent); ++i)
        dumpTreeRecursive(model, model->index(i, 0, parent), curPath, i);
}

void UEFIEdit::dumpTree()
{
    UModelIndex root = model->index(0, 0);
    if (!root.isValid())
        return;
    std::cout << "0 type=" << (int)model->type(root) << " (" << typeName(model->type(root)) << ")"
              << " name='" << model->name(root).toLocal8Bit() << "'"
              << " children=" << model->rowCount(root) << std::endl;
    for (int i = 0; i < model->rowCount(root); ++i)
        dumpTreeRecursive(model, model->index(i, 0, root), "", i);
}

static void listTreeRecursive(TreeModel * model, const UModelIndex & parent,
                              const std::string & path)
{
    if (!parent.isValid())
        return;
    UINT8 t = model->type(parent);
    UINT8 st = model->subtype(parent);

    // GUID column: file GUID, volume FvName, or GUIDed-section GUID if available.
    std::string guid = "-";
    if (t == Types::File && !model->hasEmptyHeader(parent)) {
        UByteArray hdr = model->header(parent);
        if ((UINT32)hdr.size() >= sizeof(EFI_GUID))
            guid = guidToUString(*(const EFI_GUID *)hdr.constData()).toLocal8Bit();
    }
    else if (t == Types::Volume && !model->hasEmptyParsingData(parent)) {
        UByteArray pdata = model->parsingData(parent);
        if ((UINT32)pdata.size() >= sizeof(VOLUME_PARSING_DATA)) {
            const VOLUME_PARSING_DATA *vpd = (const VOLUME_PARSING_DATA *)pdata.constData();
            if (vpd->hasExtendedHeader)
                guid = guidToUString(vpd->extendedHeaderGuid).toLocal8Bit();
        }
    }
    else if (t == Types::Section && !model->hasEmptyParsingData(parent)) {
        UByteArray pdata = model->parsingData(parent);
        if ((UINT32)pdata.size() >= sizeof(EFI_GUID))
            guid = guidToUString(*(const EFI_GUID *)pdata.constData()).toLocal8Bit();
    }

    std::cout << path << "\t" << typeName(t) << "\t" << (int)st << "\t"
              << guid << "\t0x" << std::hex << model->offset(parent) << "\t0x"
              << std::hex << model->fullSize(parent) << "\t"
              << model->name(parent).toLocal8Bit() << std::endl;

    for (int i = 0; i < model->rowCount(parent); ++i)
        listTreeRecursive(model, model->index(i, 0, parent), path + "/" + std::to_string(i));
}

void UEFIEdit::listTree()
{
    std::cout << "path\ttype\tsubtype\tguid\toffset\tsize\tname" << std::endl;
    UModelIndex root = model->index(0, 0);
    if (root.isValid())
        listTreeRecursive(model, root, "0");
}
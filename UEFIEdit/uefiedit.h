/* uefiedit.h

Copyright (c) 2026, LongSoft. All rights reserved.
This program and the accompanying materials
are licensed and made available under the terms and conditions of the BSD License
which accompanies this distribution.  The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php

THE PROGRAM IS DISTRIBUTED ON THE BSD LICENSE ON AN "AS IS" BASIS,
WITHWARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.
*/

#ifndef UEFIEDIT_H
#define UEFIEDIT_H

#include <vector>

#include "../common/basetypes.h"
#include "../common/ustring.h"
#include "../common/ubytearray.h"
#include "../common/umemstream.h"
#include "../common/filesystem.h"
#include "../common/treemodel.h"
#include "../common/parsingdata.h"
#include "../common/ffsparser.h"
#include "../common/ffsops.h"
#include "../common/ffsbuilder.h"
#include "../common/ffs.h"
#include "../common/utility.h"

class UEFIEdit
{
public:
    explicit UEFIEdit();
    ~UEFIEdit();

    USTATUS init(const UString & imagePath);
    USTATUS save(const UString & outputPath);

    // Insert a file or section. The dataPath file is parsed:
    //   - if the target is a Volume, the data must be a FFS file (.ffs)
    //   - if the target is a File or encapsulation Section, the data must be a section (.sct)
    // mode is one of CREATE_MODE_PREPEND / CREATE_MODE_BEFORE / CREATE_MODE_AFTER.
    // target selects the reference item by GUID, tree path "0/2/207", or "GUID:sectionType".
    USTATUS insert(const UString & target, const UINT8 mode, const UString & dataPath);

    // Remove the item identified by target (GUID / path / GUID:sectionType).
    USTATUS remove(const UString & target);

    // Replace body or full item (as-is) for the item identified by target.
    USTATUS replace(const UString & target, const UINT8 mode, const UString & dataPath);

    // Mark an item (and its ancestors) for rebuild.
    USTATUS rebuild(const UString & target);

    // Print the tree to stdout with path prefixes for easy copy/paste.
    void dumpTree();

    // Print a TSV-formatted listing of all items to stdout.
    void listTree();

    // Universal target resolution.
    // A target string can be:
    //   "XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX"  — find item by GUID (File/Volume/GUIDed section)
    //   "0/2/207/1/0"                           — descend the tree by child indices from root
    //   "GUID:sectionType[:index]"              — find file by GUID, then Nth section of that type
    struct Target {
        enum Kind { Guid, Path, GuidSection };
        Kind kind;
        EFI_GUID guid;
        std::vector<int> path;
        UINT8 sectionType;
        int sectionIndex;
    };

private:
    TreeModel* model;
    FfsParser* ffsParser;
    FfsOperations* ffsOps;
    FfsBuilder* ffsBuilder;
    UByteArray originalBuffer;
    bool initDone;

    bool parseTarget(const UString & s, Target & t);
    UModelIndex resolveTarget(const Target & t);
    UModelIndex findItem(const UString & target);

    // Low-level helpers kept for the GUID-only path.
    UModelIndex findItemByGuid(const UString & guidStr);
    UModelIndex findItemByGuidRecursive(const UModelIndex & parent, const EFI_GUID & guid);
    UModelIndex findSectionByTypeRecursive(const UModelIndex & parent, const UINT8 sectionType, int & index);
};

#endif // UEFIEDIT_H
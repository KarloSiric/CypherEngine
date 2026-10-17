//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_KeyValueSource_Tests.cpp
//  Purpose: Tests CYKV 2 include and base source resolution.
//  Details: Covers typed namespaces, inheritance precedence, transitive graphs,
//           cycles, loader errors, limits, schema checks, and transactions.
//
//  History:
//  - Created by Karlo Siric on 2026-09-18
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_KeyValueSource.h"
#include "CypherCommon_KeyValueWriter.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace cypher::common;

namespace
{

struct source_entry_t {
    const char *pRequest{ nullptr };
    const char *pCanonical{ nullptr };
    const char *pText{ nullptr };
    usize cchCanonical{ 0u };
    const char *pRequesting{ nullptr };
};

struct source_fixture_t {
    const source_entry_t *pEntries{ nullptr };
    usize nEntries{ 0u };
    usize nOpened{ 0u };
    usize nReleased{ 0u };
    usize nEdges{ 0u };
    bool_t bRejectDependency{ CY_FALSE };
    char callbackOrder[64]{};
    usize nCallbackEvents{ 0u };
};

void RecordCallbackEvent( source_fixture_t &fixture, char event ) noexcept
{
    if ( fixture.nCallbackEvents < sizeof( fixture.callbackOrder ) ) {
        fixture.callbackOrder[fixture.nCallbackEvents++] = event;
    }
}

key_value_source_open_status_t OpenSource(
    string_view_t requestingSourcePath,
    string_view_t referencedPath,
    key_value_source_view_t *pOutSource,
    void *pUserData ) noexcept
{
    if ( pOutSource == nullptr || pUserData == nullptr ) {
        return key_value_source_open_status_t::IO_ERROR;
    }
    auto &fixture = *static_cast<source_fixture_t *>( pUserData );
    RecordCallbackEvent( fixture, 'O' );
    for ( usize iEntry = 0u; iEntry < fixture.nEntries; ++iEntry ) {
        const source_entry_t &entry = fixture.pEntries[iEntry];
        if ( StringView_Equals(
                 referencedPath,
                 StringView_FromCString( entry.pRequest ) ) &&
             ( entry.pRequesting == nullptr ||
               StringView_Equals(
                   requestingSourcePath,
                   StringView_FromCString( entry.pRequesting ) ) ) ) {
            ++fixture.nOpened;
            *pOutSource = {
                entry.cchCanonical != 0u
                    ? string_view_t{ entry.pCanonical, entry.cchCanonical }
                    : StringView_FromCString( entry.pCanonical ),
                StringView_FromCString( entry.pText ),
                const_cast<source_entry_t *>( &entry )
            };
            return key_value_source_open_status_t::OK;
        }
    }
    return key_value_source_open_status_t::NOT_FOUND;
}

void ReleaseSource(
    const key_value_source_view_t *pSource,
    void *pUserData ) noexcept
{
    if ( pSource != nullptr && pUserData != nullptr ) {
        auto &fixture = *static_cast<source_fixture_t *>( pUserData );
        ++fixture.nReleased;
        RecordCallbackEvent( fixture, 'R' );
    }
}

bool_t RecordDependency(
    string_view_t,
    string_view_t,
    key_value_dependency_kind_t,
    void *pUserData ) noexcept
{
    auto &fixture = *static_cast<source_fixture_t *>( pUserData );
    ++fixture.nEdges;
    RecordCallbackEvent( fixture, 'D' );
    return !fixture.bRejectDependency;
}

key_value_document_t *CreateDocument(
    bool_t bCaseInsensitiveKeys = CY_FALSE )
{
    key_value_document_t *pDocument = KeyValue_CreateDocument({
        nullptr,
        128u,
        8u * CY_KIB,
        bCaseInsensitiveKeys
    });
    REQUIRE( pDocument != nullptr );
    return pDocument;
}

key_value_t *FindRequired( key_value_t *pObject, const char *pName )
{
    key_value_t *pValue = KeyValue_Find(
        pObject,
        StringView_FromCString( pName ) );
    REQUIRE( pValue != nullptr );
    return pValue;
}

key_value_source_options_t SourceOptions( source_fixture_t &fixture )
{
    key_value_source_options_t options{};
    options.pfnOpen = OpenSource;
    options.pfnRelease = ReleaseSource;
    options.pfnDependency = RecordDependency;
    options.pUserData = &fixture;
    return options;
}

key_value_source_result_t ParseSource(
    const char *pRootPath,
    const char *pText,
    source_fixture_t &fixture,
    key_value_document_t *pDocument,
    key_value_source_options_t *pOptions = nullptr )
{
    key_value_source_options_t options = pOptions != nullptr
        ? *pOptions
        : SourceOptions( fixture );
    return KeyValue_ParseSource(
        StringView_FromCString( pRootPath ),
        StringView_FromCString( pText ),
        options,
        pDocument );
}

} // namespace

TEST_CASE( "CYKV 2 resolves typed include namespaces and base inheritance",
           "[CypherCommon][Tier1][CYKV][Source]" )
{
    constexpr const char *pCommon = R"cykv(@cykv 2
@schema "cypher.shared" 1
{
    palette = { accent = [0.25, 0.5, 1.0] }
    threshold = 7u
}
)cykv";
    constexpr const char *pBase = R"cykv(@cykv 2
@schema "cypher.test" 1
{
    name = "base"
    health = 100u
    nested = { from_base = 7 local = false }
    tags = ["base"]
}
)cykv";
    constexpr const char *pRoot = R"cykv(@cykv 2
@schema "cypher.test" 1

#include "common.cydf" as common
#base "base.cydf"
#define LOCAL_SCALE 2.0

{
    name = "local"
    scale = $LOCAL_SCALE
    color = $common.palette.accent
    nested = { local = true }
}
)cykv";
    const source_entry_t entries[]{
        { "common.cydf", "data/common.cydf", pCommon },
        { "base.cydf", "data/base.cydf", pBase }
    };
    source_fixture_t fixture{ entries, 2u };
    key_value_document_t *pDocument = CreateDocument();

    const key_value_source_result_t result = ParseSource(
        "data/root.cydf",
        pRoot,
        fixture,
        pDocument );
    REQUIRE( result.status == key_value_source_status_t::OK );
    CHECK( result.nUniqueSources == 3u );
    CHECK( result.nDependencyEdges == 2u );
    CHECK( fixture.nOpened == 2u );
    CHECK( fixture.nReleased == 2u );
    CHECK( fixture.nEdges == 2u );

    const key_value_document_header_t header =
        KeyValue_DocumentHeader( pDocument );
    CHECK( header.nLanguageVersion == CYKV_LANGUAGE_VERSION_2 );
    CHECK( header.nSchemaVersion == 1u );
    CHECK( StringView_Equals(
        header.schemaId,
        StringView_FromCString( "cypher.test" ) ) );

    key_value_t *pRootValue = KeyValue_Root( pDocument );
    string_view_t name{};
    REQUIRE( KeyValue_GetString(
        FindRequired( pRootValue, "name" ),
        &name ) );
    CHECK( StringView_Equals( name, StringView_FromCString( "local" ) ) );

    u64 health = 0u;
    REQUIRE( KeyValue_GetU64(
        FindRequired( pRootValue, "health" ),
        &health ) );
    CHECK( health == 100u );

    f64 scale = 0.0;
    REQUIRE( KeyValue_GetF64(
        FindRequired( pRootValue, "scale" ),
        &scale ) );
    CHECK( scale == 2.0 );

    key_value_t *pColor = FindRequired( pRootValue, "color" );
    REQUIRE( KeyValue_Type( pColor ) == key_value_type_t::ARRAY );
    REQUIRE( KeyValue_ChildCount( pColor ) == 3u );
    f64 blue = 0.0;
    REQUIRE( KeyValue_GetF64( KeyValue_ChildAt( pColor, 2u ), &blue ) );
    CHECK( blue == 1.0 );

    key_value_t *pNested = FindRequired( pRootValue, "nested" );
    bool_t local = CY_FALSE;
    REQUIRE( KeyValue_GetBool( FindRequired( pNested, "local" ), &local ) );
    CHECK( local );
    i64 fromBase = 0;
    REQUIRE( KeyValue_GetI64(
        FindRequired( pNested, "from_base" ),
        &fromBase ) );
    CHECK( fromBase == 7 );
    REQUIRE( KeyValue_Type( FindRequired( pRootValue, "tags" ) ) ==
             key_value_type_t::ARRAY );

    char written[2048]{};
    const key_value_write_result_t writeResult = KeyValue_WriteText(
        pRootValue,
        {},
        written,
        sizeof( written ) );
    REQUIRE( writeResult.status == key_value_write_status_t::OK );
    CHECK( StringView_Equals(
        { written, 7u },
        StringView_FromCString( "@cykv 2" ) ) );
    const key_value_canonical_hash_result_t hashResult =
        KeyValue_HashCanonicalDocument( pDocument );
    CHECK( hashResult.status == key_value_write_status_t::OK );
    CHECK( hashResult.cbHashed != 0u );

    KeyValue_DestroyDocument( pDocument );
}

TEST_CASE( "CYKV 2 applies earlier bases before later bases",
           "[CypherCommon][Tier1][CYKV][Source][Base]" )
{
    constexpr const char *pBase1 =
        "@cykv 2\n@schema \"cypher.test\" 1\n"
        "{ priority = 1 shared = { first = 1 } }";
    constexpr const char *pBase2 =
        "@cykv 2\n@schema \"cypher.test\" 1\n"
        "{ priority = 2 shared = { first = 2 second = 2 } }";
    constexpr const char *pRoot =
        "@cykv 2\n@schema \"cypher.test\" 1\n"
        "#base \"first.cydf\"\n#base \"second.cydf\"\n{}";
    const source_entry_t entries[]{
        { "first.cydf", "data/first.cydf", pBase1 },
        { "second.cydf", "data/second.cydf", pBase2 }
    };
    source_fixture_t fixture{ entries, 2u };
    key_value_document_t *pDocument = CreateDocument();

    REQUIRE( ParseSource(
        "data/root.cydf",
        pRoot,
        fixture,
        pDocument ).status == key_value_source_status_t::OK );
    key_value_t *pRootValue = KeyValue_Root( pDocument );
    i64 priority = 0;
    REQUIRE( KeyValue_GetI64(
        FindRequired( pRootValue, "priority" ),
        &priority ) );
    CHECK( priority == 1 );
    key_value_t *pShared = FindRequired( pRootValue, "shared" );
    i64 value = 0;
    REQUIRE( KeyValue_GetI64( FindRequired( pShared, "first" ), &value ) );
    CHECK( value == 1 );
    REQUIRE( KeyValue_GetI64( FindRequired( pShared, "second" ), &value ) );
    CHECK( value == 2 );

    KeyValue_DestroyDocument( pDocument );
}

TEST_CASE( "CYKV 2 accepts an included namespace as the complete root",
           "[CypherCommon][Tier1][CYKV][Source][Include]" )
{
    constexpr const char *pShared =
        "@cykv 2\n@schema \"cypher.shared\" 1\n"
        "{ name = \"shared-root\" nested = { enabled = true } }";
    constexpr const char *pRoot =
        "@cykv 2\n@schema \"cypher.test\" 1\n"
        "#include \"shared.cydf\" as shared\n"
        "$shared";
    const source_entry_t entries[]{
        { "shared.cydf", "data/shared.cydf", pShared }
    };
    source_fixture_t fixture{ entries, 1u };
    key_value_document_t *pDocument = CreateDocument();

    const key_value_source_result_t result = ParseSource(
        "data/root.cydf",
        pRoot,
        fixture,
        pDocument );
    REQUIRE( result.status == key_value_source_status_t::OK );
    CHECK( fixture.nOpened == 1u );
    CHECK( fixture.nReleased == 1u );

    const key_value_document_header_t header =
        KeyValue_DocumentHeader( pDocument );
    CHECK( StringView_Equals(
        header.schemaId,
        StringView_FromCString( "cypher.test" ) ) );
    string_view_t name{};
    REQUIRE( KeyValue_GetString(
        FindRequired( KeyValue_Root( pDocument ), "name" ),
        &name ) );
    CHECK( StringView_Equals(
        name,
        StringView_FromCString( "shared-root" ) ) );
    bool_t enabled = CY_FALSE;
    REQUIRE( KeyValue_GetBool(
        FindRequired(
            FindRequired( KeyValue_Root( pDocument ), "nested" ),
            "enabled" ),
        &enabled ) );
    CHECK( enabled );

    KeyValue_DestroyDocument( pDocument );
}

TEST_CASE( "CYKV include symbols remain case-sensitive in insensitive documents",
           "[CypherCommon][Tier1][CYKV][Source][Include]" )
{
    constexpr const char *pShared =
        "@cykv 2\n@schema \"cypher.shared\" 1\n{ Member = 7 }";
    const source_entry_t entries[]{
        { "shared.cydf", "data/shared.cydf", pShared }
    };

    SECTION( "included object keys retain exact-case identity" ) {
        constexpr const char *pCaseShared =
            "@cykv 2\n@schema \"cypher.shared\" 1\n"
            "{ Member = 7 member = 8 }";
        constexpr const char *pRoot =
            "@cykv 2\n@schema \"cypher.test\" 1\n"
            "#include \"case-shared.cydf\" as shared\n"
            "{ upper = $shared.Member lower = $shared.member }";
        const source_entry_t caseEntries[]{
            { "case-shared.cydf", "data/case-shared.cydf", pCaseShared }
        };
        source_fixture_t fixture{ caseEntries, 1u };
        key_value_document_t *pDocument = CreateDocument( CY_TRUE );
        const key_value_source_result_t result = ParseSource(
            "data/root.cydf",
            pRoot,
            fixture,
            pDocument );
        REQUIRE( result.status == key_value_source_status_t::OK );
        i64 upper = 0;
        i64 lower = 0;
        REQUIRE( KeyValue_GetI64(
            FindRequired( KeyValue_Root( pDocument ), "upper" ),
            &upper ) );
        REQUIRE( KeyValue_GetI64(
            FindRequired( KeyValue_Root( pDocument ), "lower" ),
            &lower ) );
        CHECK( upper == 7 );
        CHECK( lower == 8 );
        KeyValue_DestroyDocument( pDocument );
    }

    SECTION( "exact alias and member spelling resolves" ) {
        constexpr const char *pRoot =
            "@cykv 2\n@schema \"cypher.test\" 1\n"
            "#include \"shared.cydf\" as shared\n"
            "{ value = $shared.Member }";
        source_fixture_t fixture{ entries, 1u };
        key_value_document_t *pDocument = CreateDocument( CY_TRUE );
        const key_value_source_result_t result = ParseSource(
            "data/root.cydf",
            pRoot,
            fixture,
            pDocument );
        REQUIRE( result.status == key_value_source_status_t::OK );
        i64 value = 0;
        REQUIRE( KeyValue_GetI64(
            FindRequired( KeyValue_Root( pDocument ), "value" ),
            &value ) );
        CHECK( value == 7 );
        KeyValue_DestroyDocument( pDocument );
    }

    SECTION( "alias case mismatch remains undefined" ) {
        constexpr const char *pRoot =
            "@cykv 2\n@schema \"cypher.test\" 1\n"
            "#include \"shared.cydf\" as shared\n"
            "{ value = $Shared.Member }";
        source_fixture_t fixture{ entries, 1u };
        key_value_document_t *pDocument = CreateDocument( CY_TRUE );
        const key_value_source_result_t result = ParseSource(
            "data/root.cydf",
            pRoot,
            fixture,
            pDocument );
        CHECK( result.status == key_value_source_status_t::PARSE_FAILED );
        CHECK( result.parseResult.status ==
               key_value_parse_status_t::UNDEFINED_DEFINITION );
        KeyValue_DestroyDocument( pDocument );
    }

    SECTION( "member case mismatch remains undefined" ) {
        constexpr const char *pRoot =
            "@cykv 2\n@schema \"cypher.test\" 1\n"
            "#include \"shared.cydf\" as shared\n"
            "{ value = $shared.member }";
        source_fixture_t fixture{ entries, 1u };
        key_value_document_t *pDocument = CreateDocument( CY_TRUE );
        const key_value_source_result_t result = ParseSource(
            "data/root.cydf",
            pRoot,
            fixture,
            pDocument );
        CHECK( result.status == key_value_source_status_t::PARSE_FAILED );
        CHECK( result.parseResult.status ==
               key_value_parse_status_t::UNDEFINED_DEFINITION );
        KeyValue_DestroyDocument( pDocument );
    }
}

TEST_CASE( "CYKV source resolves the same relative spelling from each parent",
           "[CypherCommon][Tier1][CYKV][Source][Callbacks]" )
{
    constexpr const char *pCommonA =
        "@cykv 2\n@schema \"cypher.shared\" 1\n{ value = 11 }";
    constexpr const char *pCommonB =
        "@cykv 2\n@schema \"cypher.shared\" 1\n{ value = 22 }";
    constexpr const char *pParent =
        "@cykv 2\n@schema \"cypher.parent\" 1\n"
        "#include \"common.cydf\" as common\n"
        "{ value = $common.value }";
    constexpr const char *pRoot =
        "@cykv 2\n@schema \"cypher.test\" 1\n"
        "#include \"a.cydf\" as a\n"
        "#include \"b.cydf\" as b\n"
        "{ a_value = $a.value b_value = $b.value }";
    const source_entry_t entries[]{
        { "a.cydf", "data/a/parent.cydf", pParent, 0u,
          "data/root.cydf" },
        { "b.cydf", "data/b/parent.cydf", pParent, 0u,
          "data/root.cydf" },
        { "common.cydf", "data/a/common.cydf", pCommonA, 0u,
          "data/a/parent.cydf" },
        { "common.cydf", "data/b/common.cydf", pCommonB, 0u,
          "data/b/parent.cydf" }
    };
    source_fixture_t fixture{ entries, 4u };
    key_value_document_t *pDocument = CreateDocument();

    const key_value_source_result_t result = ParseSource(
        "data/root.cydf",
        pRoot,
        fixture,
        pDocument );
    REQUIRE( result.status == key_value_source_status_t::OK );
    CHECK( result.nUniqueSources == 5u );
    CHECK( result.nDependencyEdges == 4u );
    CHECK( fixture.nOpened == 4u );
    CHECK( fixture.nReleased == 4u );
    CHECK( fixture.nEdges == 4u );
    CHECK( StringView_Equals(
        { fixture.callbackOrder, fixture.nCallbackEvents },
        StringView_FromCString( "ODODRRODODRR" ) ) );

    i64 value = 0;
    REQUIRE( KeyValue_GetI64(
        FindRequired( KeyValue_Root( pDocument ), "a_value" ),
        &value ) );
    CHECK( value == 11 );
    REQUIRE( KeyValue_GetI64(
        FindRequired( KeyValue_Root( pDocument ), "b_value" ),
        &value ) );
    CHECK( value == 22 );

    KeyValue_DestroyDocument( pDocument );
}

TEST_CASE( "CYKV source reports every edge while deduplicating canonical sources",
           "[CypherCommon][Tier1][CYKV][Source][Callbacks]" )
{
    constexpr const char *pShared =
        "@cykv 2\n@schema \"cypher.shared\" 1\n{ value = 7 }";
    constexpr const char *pRoot =
        "@cykv 2\n@schema \"cypher.test\" 1\n"
        "#include \"one.cydf\" as one\n"
        "#include \"two.cydf\" as two\n"
        "{ first = $one.value second = $two.value }";
    const source_entry_t entries[]{
        { "one.cydf", "data/shared.cydf", pShared },
        { "two.cydf", "data/shared.cydf", pShared }
    };
    source_fixture_t fixture{ entries, 2u };
    key_value_document_t *pDocument = CreateDocument();

    const key_value_source_result_t result = ParseSource(
        "data/root.cydf",
        pRoot,
        fixture,
        pDocument );
    REQUIRE( result.status == key_value_source_status_t::OK );
    CHECK( result.nUniqueSources == 2u );
    CHECK( result.nDependencyEdges == 2u );
    CHECK( fixture.nOpened == 2u );
    CHECK( fixture.nReleased == 2u );
    CHECK( fixture.nEdges == 2u );
    CHECK( StringView_Equals(
        { fixture.callbackOrder, fixture.nCallbackEvents },
        StringView_FromCString( "ODRODR" ) ) );

    i64 first = 0;
    i64 second = 0;
    REQUIRE( KeyValue_GetI64(
        FindRequired( KeyValue_Root( pDocument ), "first" ),
        &first ) );
    REQUIRE( KeyValue_GetI64(
        FindRequired( KeyValue_Root( pDocument ), "second" ),
        &second ) );
    CHECK( first == 7 );
    CHECK( second == 7 );

    KeyValue_DestroyDocument( pDocument );
}

TEST_CASE( "CYKV 2 base merge accepts a container at the depth boundary",
           "[CypherCommon][Tier1][CYKV][Source][Base][Limits]" )
{
    constexpr const char *pBase =
        "@cykv 2\n@schema \"cypher.test\" 1\n"
        "{ branch = { leaf = { value = 7 } } }";
    constexpr const char *pRoot =
        "@cykv 2\n@schema \"cypher.test\" 1\n"
        "#base \"base.cydf\"\n{ branch = {} }";
    const source_entry_t entries[]{
        { "base.cydf", "data/base.cydf", pBase }
    };
    source_fixture_t fixture{ entries, 1u };
    key_value_source_options_t options = SourceOptions( fixture );
    options.parseOptions.nMaxDepth = 2u;
    key_value_document_t *pDocument = CreateDocument();

    const key_value_source_result_t result = ParseSource(
        "data/root.cydf",
        pRoot,
        fixture,
        pDocument,
        &options );
    REQUIRE( result.status == key_value_source_status_t::OK );
    const key_value_t *pBranch = KeyValue_Find(
        KeyValue_Root( pDocument ),
        StringView_FromCString( "branch" ) );
    REQUIRE( pBranch != nullptr );
    const key_value_t *pLeaf = KeyValue_Find(
        pBranch,
        StringView_FromCString( "leaf" ) );
    REQUIRE( pLeaf != nullptr );
    i64 value = 0;
    REQUIRE( KeyValue_GetI64(
        KeyValue_Find( pLeaf, StringView_FromCString( "value" ) ),
        &value ) );
    CHECK( value == 7 );

    KeyValue_DestroyDocument( pDocument );
}

TEST_CASE( "CYKV 2 source resolver rejects cycles missing sources and bad graphs",
           "[CypherCommon][Tier1][CYKV][Source][Diagnostics]" )
{
    SECTION( "include cycle" ) {
        constexpr const char *pRoot =
            "@cykv 2\n@schema \"cypher.test\" 1\n"
            "#include \"b.cydf\" as b\n{}";
        constexpr const char *pB =
            "@cykv 2\n@schema \"cypher.test\" 1\n"
            "#include \"a.cydf\" as a\n{}";
        const source_entry_t entries[]{
            { "b.cydf", "data/b.cydf", pB },
            { "a.cydf", "data/root.cydf", pRoot }
        };
        source_fixture_t fixture{ entries, 2u };
        key_value_document_t *pDocument = CreateDocument();
        const key_value_source_result_t result = ParseSource(
            "data/root.cydf",
            pRoot,
            fixture,
            pDocument );
        CHECK( result.status == key_value_source_status_t::DEPENDENCY_CYCLE );
        CHECK( StringView_Equals(
            {
                result.errorReferencedPath,
                result.cchErrorReferencedPath
            },
            StringView_FromCString( "a.cydf" ) ) );
        CHECK( fixture.nOpened == fixture.nReleased );
        KeyValue_DestroyDocument( pDocument );
    }

    SECTION( "missing source" ) {
        constexpr const char *pRoot =
            "@cykv 2\n@schema \"cypher.test\" 1\n"
            "#base \"missing.cydf\"\n{}";
        source_fixture_t fixture{};
        key_value_document_t *pDocument = CreateDocument();
        const key_value_source_result_t result = ParseSource(
            "data/root.cydf",
            pRoot,
            fixture,
            pDocument );
        CHECK( result.status == key_value_source_status_t::OPEN_FAILED );
        CHECK( result.openStatus == key_value_source_open_status_t::NOT_FOUND );
        CHECK( result.dependencyKind == key_value_dependency_kind_t::BASE );
        KeyValue_DestroyDocument( pDocument );
    }

    SECTION( "duplicate include alias" ) {
        constexpr const char *pValue =
            "@cykv 2\n@schema \"cypher.shared\" 1\n{}";
        constexpr const char *pRoot =
            "@cykv 2\n@schema \"cypher.test\" 1\n"
            "#include \"a.cydf\" as shared\n"
            "#include \"b.cydf\" as shared\n{}";
        const source_entry_t entries[]{
            { "a.cydf", "data/a.cydf", pValue },
            { "b.cydf", "data/b.cydf", pValue }
        };
        source_fixture_t fixture{ entries, 2u };
        key_value_document_t *pDocument = CreateDocument();
        CHECK( ParseSource(
            "data/root.cydf",
            pRoot,
            fixture,
            pDocument ).status == key_value_source_status_t::DUPLICATE_ALIAS );
        CHECK( fixture.nOpened == 0u );
        KeyValue_DestroyDocument( pDocument );
    }

    SECTION( "include namespace collides with local definition" ) {
        constexpr const char *pValue =
            "@cykv 2\n@schema \"cypher.shared\" 1\n{ value = 7 }";
        constexpr const char *pRoot =
            "@cykv 2\n@schema \"cypher.test\" 1\n"
            "#include \"a.cydf\" as shared\n"
            "#define shared 9\n{ value = $shared }";
        const source_entry_t entries[]{
            { "a.cydf", "data/a.cydf", pValue }
        };
        source_fixture_t fixture{ entries, 1u };
        key_value_document_t *pDocument = CreateDocument();
        const key_value_source_result_t result = ParseSource(
            "data/root.cydf",
            pRoot,
            fixture,
            pDocument );
        CHECK( result.status == key_value_source_status_t::PARSE_FAILED );
        CHECK( result.parseResult.status ==
               key_value_parse_status_t::DUPLICATE_DEFINITION );
        KeyValue_DestroyDocument( pDocument );
    }

    SECTION( "dependency sink rejection" ) {
        constexpr const char *pValue =
            "@cykv 2\n@schema \"cypher.shared\" 1\n{}";
        constexpr const char *pRoot =
            "@cykv 2\n@schema \"cypher.test\" 1\n"
            "#include \"a.cydf\" as a\n{}";
        const source_entry_t entries[]{
            { "a.cydf", "data/a.cydf", pValue }
        };
        source_fixture_t fixture{ entries, 1u };
        fixture.bRejectDependency = CY_TRUE;
        key_value_document_t *pDocument = CreateDocument();
        CHECK( ParseSource(
            "data/root.cydf",
            pRoot,
            fixture,
            pDocument ).status ==
            key_value_source_status_t::DEPENDENCY_SINK_FAILED );
        KeyValue_DestroyDocument( pDocument );
    }
}

TEST_CASE( "CYKV source validation precedes dependency callbacks",
           "[CypherCommon][Tier1][CYKV][Source][Validation]" )
{
    constexpr const char *pDependency =
        "@cykv 2\n@schema \"cypher.shared\" 1\n{}";
    const source_entry_t entries[]{
        { "a.cydf", "data/a.cydf", pDependency },
        { "b.cydf", "data/b.cydf", pDependency }
    };

    SECTION( "invalid UTF-8" ) {
        constexpr const char pRoot[] =
            "@cykv 2\n@schema \"cypher.test\" 1\n"
            "#include \"\xFF.cydf\" as a\n{}";
        source_fixture_t fixture{ entries, 2u };
        key_value_document_t *pDocument = CreateDocument();
        const key_value_source_result_t result = ParseSource(
            "data/root.cydf",
            pRoot,
            fixture,
            pDocument );
        CHECK( result.status == key_value_source_status_t::PARSE_FAILED );
        CHECK( result.parseResult.status ==
               key_value_parse_status_t::INVALID_ENCODING );
        CHECK( fixture.nOpened == 0u );
        CHECK( fixture.nEdges == 0u );
        KeyValue_DestroyDocument( pDocument );
    }

    SECTION( "malformed schema header" ) {
        constexpr const char *pRoot =
            "@cykv 2\n@schema \"not-a-schema\" 1\n"
            "#include \"a.cydf\" as a\n{}";
        source_fixture_t fixture{ entries, 2u };
        key_value_document_t *pDocument = CreateDocument();
        const key_value_source_result_t result = ParseSource(
            "data/root.cydf",
            pRoot,
            fixture,
            pDocument );
        CHECK( result.status == key_value_source_status_t::PARSE_FAILED );
        CHECK( result.parseResult.status ==
               key_value_parse_status_t::INVALID_SCHEMA );
        CHECK( fixture.nOpened == 0u );
        KeyValue_DestroyDocument( pDocument );
    }

    SECTION( "source input limit" ) {
        constexpr const char *pRoot =
            "@cykv 2\n@schema \"cypher.test\" 1\n"
            "#include \"a.cydf\" as a\n{}";
        source_fixture_t fixture{ entries, 2u };
        key_value_source_options_t options = SourceOptions( fixture );
        options.parseOptions.cbMaxInput =
            StringView_FromCString( pRoot ).cchLength - 1u;
        key_value_document_t *pDocument = CreateDocument();
        const key_value_source_result_t result = ParseSource(
            "data/root.cydf",
            pRoot,
            fixture,
            pDocument,
            &options );
        CHECK( result.status == key_value_source_status_t::PARSE_FAILED );
        CHECK( result.parseResult.status ==
               key_value_parse_status_t::INPUT_LIMIT );
        CHECK( fixture.nOpened == 0u );
        KeyValue_DestroyDocument( pDocument );
    }

    SECTION( "invalid parse options" ) {
        constexpr const char *pRoot =
            "@cykv 2\n@schema \"cypher.test\" 1\n"
            "#include \"a.cydf\" as a\n{}";
        source_fixture_t fixture{ entries, 2u };
        key_value_source_options_t options = SourceOptions( fixture );
        options.parseOptions.nMaxDepth = 0u;
        key_value_document_t *pDocument = CreateDocument();
        const key_value_source_result_t result = ParseSource(
            "data/root.cydf",
            pRoot,
            fixture,
            pDocument,
            &options );
        CHECK( result.status == key_value_source_status_t::INVALID_ARGUMENT );
        CHECK( fixture.nOpened == 0u );
        KeyValue_DestroyDocument( pDocument );
    }

    SECTION( "comments disabled" ) {
        constexpr const char *pRoot =
            "@cykv 2\n@schema \"cypher.test\" 1\n"
            "// dependency follows\n#include \"a.cydf\" as a\n{}";
        source_fixture_t fixture{ entries, 2u };
        key_value_source_options_t options = SourceOptions( fixture );
        options.parseOptions.flags &= ~KEY_VALUE_PARSE_FLAG_ALLOW_COMMENTS;
        key_value_document_t *pDocument = CreateDocument();
        const key_value_source_result_t result = ParseSource(
            "data/root.cydf",
            pRoot,
            fixture,
            pDocument,
            &options );
        CHECK( result.status == key_value_source_status_t::PARSE_FAILED );
        CHECK( fixture.nOpened == 0u );
        KeyValue_DestroyDocument( pDocument );
    }

    SECTION( "trailing directive comments disabled" ) {
        constexpr const char *pRoot =
            "@cykv 2\n@schema \"cypher.test\" 1\n"
            "#include \"a.cydf\" as a // comment\n{}";
        source_fixture_t fixture{ entries, 2u };
        key_value_source_options_t options = SourceOptions( fixture );
        options.parseOptions.flags &= ~KEY_VALUE_PARSE_FLAG_ALLOW_COMMENTS;
        key_value_document_t *pDocument = CreateDocument();
        const key_value_source_result_t result = ParseSource(
            "data/root.cydf",
            pRoot,
            fixture,
            pDocument,
            &options );
        CHECK( result.status ==
               key_value_source_status_t::INVALID_DIRECTIVE );
        CHECK( fixture.nOpened == 0u );
        KeyValue_DestroyDocument( pDocument );
    }

    SECTION( "preamble comment depth" ) {
        constexpr const char *pRoot =
            "@cykv 2\n@schema \"cypher.test\" 1\n"
            "/* outer /* nested */ outer */\n"
            "#include \"a.cydf\" as a\n{}";
        source_fixture_t fixture{ entries, 2u };
        key_value_source_options_t options = SourceOptions( fixture );
        options.parseOptions.nMaxCommentDepth = 1u;
        key_value_document_t *pDocument = CreateDocument();
        const key_value_source_result_t result = ParseSource(
            "data/root.cydf",
            pRoot,
            fixture,
            pDocument,
            &options );
        CHECK( result.status == key_value_source_status_t::PARSE_FAILED );
        CHECK( result.parseResult.status ==
               key_value_parse_status_t::COMMENT_DEPTH_LIMIT );
        CHECK( fixture.nOpened == 0u );
        KeyValue_DestroyDocument( pDocument );
    }

    SECTION( "unknown preamble directive" ) {
        constexpr const char *pRoot =
            "@cykv 2\n@schema \"cypher.test\" 1\n"
            "#include \"a.cydf\" as a\n#unknown value\n{}";
        source_fixture_t fixture{ entries, 2u };
        key_value_document_t *pDocument = CreateDocument();
        const key_value_source_result_t result = ParseSource(
            "data/root.cydf",
            pRoot,
            fixture,
            pDocument );
        CHECK( result.status ==
               key_value_source_status_t::INVALID_DIRECTIVE );
        CHECK( fixture.nOpened == 0u );
        KeyValue_DestroyDocument( pDocument );
    }
}

TEST_CASE( "CYKV dependency scanner preserves locations across multiline preambles",
           "[CypherCommon][Tier1][CYKV][Source][Diagnostics]" )
{
    constexpr const char pPrefix[] =
        "@cykv 2\n"
        "@schema \"cypher.test\" 1\n"
        "#include \"a.cydf\" as a\n"
        "// spacer\n"
        "#include \"b.cydf\" as b\n"
        "/* multi-line\n"
        "   spacer */\n"
        "#base \"defaults.cydf\"\n";
    constexpr const char pRoot[] =
        "@cykv 2\n"
        "@schema \"cypher.test\" 1\n"
        "#include \"a.cydf\" as a\n"
        "// spacer\n"
        "#include \"b.cydf\" as b\n"
        "/* multi-line\n"
        "   spacer */\n"
        "#base \"defaults.cydf\"\n"
        "#unknown value\n"
        "{}";
    source_fixture_t fixture{};
    key_value_document_t *pDocument = CreateDocument();

    const key_value_source_result_t result = ParseSource(
        "data/root.cydf",
        pRoot,
        fixture,
        pDocument );
    CHECK( result.status == key_value_source_status_t::INVALID_DIRECTIVE );
    CHECK( result.errorLocation.iByte == sizeof( pPrefix ) - 1u );
    CHECK( result.errorLocation.nLine == 9u );
    CHECK( result.errorLocation.nColumn == 1u );
    CHECK( fixture.nOpened == 0u );

    KeyValue_DestroyDocument( pDocument );
}

TEST_CASE( "CYKV source rejects unsafe dependency path bytes",
           "[CypherCommon][Tier1][CYKV][Source][Path]" )
{
    constexpr const char *pValue =
        "@cykv 2\n@schema \"cypher.shared\" 1\n{}";
    constexpr const char *pRoot =
        "@cykv 2\n@schema \"cypher.test\" 1\n"
        "#include \"value.cydf\" as value\n{}";

    SECTION( "authored dependency path contains DEL" ) {
        constexpr const char pBadRoot[] =
            "@cykv 2\n@schema \"cypher.test\" 1\n"
            "#include \"value\x7f.cydf\" as value\n{}";
        source_fixture_t fixture{};
        key_value_document_t *pDocument = CreateDocument();
        const key_value_source_result_t result = ParseSource(
            "data/root.cydf",
            pBadRoot,
            fixture,
            pDocument );
        CHECK( result.status ==
               key_value_source_status_t::INVALID_DIRECTIVE );
        CHECK( fixture.nOpened == 0u );
        CHECK( fixture.nReleased == 0u );
        KeyValue_DestroyDocument( pDocument );
    }

    SECTION( "root canonical path contains a control byte" ) {
        constexpr const char badRootPath[]{
            'd', 'a', 't', 'a', '/', 'r', 'o', 'o', 't', '\n', '.', 'c', 'y', 'd', 'f'
        };
        source_fixture_t fixture{};
        key_value_source_options_t options = SourceOptions( fixture );
        key_value_document_t *pDocument = CreateDocument();
        const key_value_source_result_t result = KeyValue_ParseSource(
            { badRootPath, sizeof( badRootPath ) },
            StringView_FromCString( pValue ),
            options,
            pDocument );
        CHECK( result.status == key_value_source_status_t::INVALID_PATH );
        CHECK( fixture.nOpened == 0u );
        CHECK( result.cchErrorSourcePath == sizeof( badRootPath ) );
        CHECK( result.errorSourcePath[9] == '?' );
        KeyValue_DestroyDocument( pDocument );
    }

    SECTION( "loader canonical path contains an embedded NUL" ) {
        constexpr const char badCanonical[]{
            'd', 'a', 't', 'a', '/', 'v', 'a', 'l', 'u', 'e', '\0', '.', 'c', 'y', 'd', 'f'
        };
        const source_entry_t entries[]{
            { "value.cydf", badCanonical, pValue, sizeof( badCanonical ) }
        };
        source_fixture_t fixture{ entries, 1u };
        key_value_document_t *pDocument = CreateDocument();
        const key_value_source_result_t result = ParseSource(
            "data/root.cydf",
            pRoot,
            fixture,
            pDocument );
        CHECK( result.status == key_value_source_status_t::INVALID_PATH );
        CHECK( fixture.nOpened == 1u );
        CHECK( fixture.nReleased == 1u );
        KeyValue_DestroyDocument( pDocument );
    }

    SECTION( "loader canonical path contains a backslash" ) {
        const source_entry_t entries[]{
            { "value.cydf", "data\\value.cydf", pValue }
        };
        source_fixture_t fixture{ entries, 1u };
        key_value_document_t *pDocument = CreateDocument();
        const key_value_source_result_t result = ParseSource(
            "data/root.cydf",
            pRoot,
            fixture,
            pDocument );
        CHECK( result.status == key_value_source_status_t::INVALID_PATH );
        CHECK( fixture.nOpened == 1u );
        CHECK( fixture.nReleased == 1u );
        KeyValue_DestroyDocument( pDocument );
    }
}

TEST_CASE( "CYKV 2 bases require exact schemas and compatible types",
           "[CypherCommon][Tier1][CYKV][Source][Base]" )
{
    constexpr const char *pRoot =
        "@cykv 2\n@schema \"cypher.test\" 1\n"
        "#base \"base.cydf\"\n{ value = 1 }";

    SECTION( "schema mismatch" ) {
        constexpr const char *pBase =
            "@cykv 2\n@schema \"cypher.other\" 1\n{ value = 1 }";
        const source_entry_t entries[]{
            { "base.cydf", "data/base.cydf", pBase }
        };
        source_fixture_t fixture{ entries, 1u };
        key_value_document_t *pDocument = CreateDocument();
        CHECK( ParseSource(
            "data/root.cydf",
            pRoot,
            fixture,
            pDocument ).status == key_value_source_status_t::SCHEMA_MISMATCH );
        KeyValue_DestroyDocument( pDocument );
    }

    SECTION( "type conflict" ) {
        constexpr const char *pBase =
            "@cykv 2\n@schema \"cypher.test\" 1\n"
            "{ value = { nested = true } }";
        const source_entry_t entries[]{
            { "base.cydf", "data/base.cydf", pBase }
        };
        source_fixture_t fixture{ entries, 1u };
        key_value_document_t *pDocument = CreateDocument();
        CHECK( ParseSource(
            "data/root.cydf",
            pRoot,
            fixture,
            pDocument ).status == key_value_source_status_t::MERGE_CONFLICT );
        KeyValue_DestroyDocument( pDocument );
    }

    SECTION( "base member matching remains case-sensitive" ) {
        constexpr const char *pCaseRoot =
            "@cykv 2\n@schema \"cypher.test\" 1\n"
            "#base \"base.cydf\"\n{ value = 1 }";
        constexpr const char *pBase =
            "@cykv 2\n@schema \"cypher.test\" 1\n{ Value = 2 }";
        const source_entry_t entries[]{
            { "base.cydf", "data/base.cydf", pBase }
        };
        source_fixture_t fixture{ entries, 1u };
        key_value_document_t *pDocument = CreateDocument( CY_TRUE );
        const key_value_source_result_t result = ParseSource(
            "data/root.cydf",
            pCaseRoot,
            fixture,
            pDocument );
        REQUIRE( result.status == key_value_source_status_t::OK );
        key_value_t *pEffectiveRoot = KeyValue_Root( pDocument );
        REQUIRE( KeyValue_ChildCount( pEffectiveRoot ) == 2u );
        key_value_t *pLocal = KeyValue_ChildAt( pEffectiveRoot, 0u );
        key_value_t *pInherited = KeyValue_ChildAt( pEffectiveRoot, 1u );
        REQUIRE( pLocal != nullptr );
        REQUIRE( pInherited != nullptr );
        CHECK( StringView_Equals(
            KeyValue_Name( pLocal ),
            StringView_FromCString( "value" ) ) );
        CHECK( StringView_Equals(
            KeyValue_Name( pInherited ),
            StringView_FromCString( "Value" ) ) );
        i64 value = 0;
        REQUIRE( KeyValue_GetI64( pLocal, &value ) );
        CHECK( value == 1 );
        REQUIRE( KeyValue_GetI64( pInherited, &value ) );
        CHECK( value == 2 );
        KeyValue_DestroyDocument( pDocument );
    }

    SECTION( "merged node budget is checked before cloning" ) {
        constexpr const char *pNodeRoot =
            "@cykv 2\n@schema \"cypher.test\" 1\n"
            "#base \"base.cydf\"\n{ local = 1 }";
        constexpr const char *pBase =
            "@cykv 2\n@schema \"cypher.test\" 1\n{ inherited = 2 }";
        const source_entry_t entries[]{
            { "base.cydf", "data/base.cydf", pBase }
        };
        source_fixture_t fixture{ entries, 1u };
        key_value_source_options_t options = SourceOptions( fixture );
        options.parseOptions.nMaxNodes = 2u;
        key_value_document_t *pDocument = CreateDocument();
        const key_value_source_result_t result = ParseSource(
            "data/root.cydf",
            pNodeRoot,
            fixture,
            pDocument,
            &options );
        CHECK( result.status == key_value_source_status_t::PARSE_FAILED );
        CHECK( result.parseResult.status == key_value_parse_status_t::NODE_LIMIT );
        KeyValue_DestroyDocument( pDocument );
    }

    SECTION( "merged container budget is checked before cloning" ) {
        constexpr const char *pContainerRoot =
            "@cykv 2\n@schema \"cypher.test\" 1\n"
            "#base \"base.cydf\"\n{ local = 1 }";
        constexpr const char *pBase =
            "@cykv 2\n@schema \"cypher.test\" 1\n{ inherited = 2 }";
        const source_entry_t entries[]{
            { "base.cydf", "data/base.cydf", pBase }
        };
        source_fixture_t fixture{ entries, 1u };
        key_value_source_options_t options = SourceOptions( fixture );
        options.parseOptions.nMaxContainerValues = 1u;
        key_value_document_t *pDocument = CreateDocument();
        const key_value_source_result_t result = ParseSource(
            "data/root.cydf",
            pContainerRoot,
            fixture,
            pDocument,
            &options );
        CHECK( result.status == key_value_source_status_t::PARSE_FAILED );
        CHECK( result.parseResult.status ==
               key_value_parse_status_t::CONTAINER_LIMIT );
        KeyValue_DestroyDocument( pDocument );
    }
}

TEST_CASE( "CYKV source resolution is bounded versioned and transactional",
           "[CypherCommon][Tier1][CYKV][Source][Limits]" )
{
    constexpr const char *pDependency =
        "@cykv 2\n@schema \"cypher.shared\" 1\n{}";
    const source_entry_t entries[]{
        { "a.cydf", "data/a.cydf", pDependency },
        { "b.cydf", "data/b.cydf", pDependency }
    };

    SECTION( "edge limit" ) {
        constexpr const char *pRoot =
            "@cykv 2\n@schema \"cypher.test\" 1\n"
            "#include \"a.cydf\" as a\n"
            "#include \"b.cydf\" as b\n{}";
        source_fixture_t fixture{ entries, 2u };
        key_value_source_options_t options = SourceOptions( fixture );
        options.nMaxEdges = 1u;
        key_value_document_t *pDocument = CreateDocument();
        CHECK( ParseSource(
            "data/root.cydf",
            pRoot,
            fixture,
            pDocument,
            &options ).status == key_value_source_status_t::EDGE_LIMIT );
        KeyValue_DestroyDocument( pDocument );
    }

    SECTION( "unique source limit" ) {
        constexpr const char *pRoot =
            "@cykv 2\n@schema \"cypher.test\" 1\n"
            "#include \"a.cydf\" as a\n{}";
        source_fixture_t fixture{ entries, 2u };
        key_value_source_options_t options = SourceOptions( fixture );
        options.nMaxSources = 1u;
        key_value_document_t *pDocument = CreateDocument();
        const key_value_source_result_t result = ParseSource(
            "data/root.cydf",
            pRoot,
            fixture,
            pDocument,
            &options );
        CHECK( result.status == key_value_source_status_t::SOURCE_LIMIT );
        CHECK( result.nUniqueSources == 1u );
        CHECK( fixture.nOpened == fixture.nReleased );
        KeyValue_DestroyDocument( pDocument );
    }

    SECTION( "aggregate source byte limit counts the root" ) {
        constexpr const char *pRoot =
            "@cykv 2\n@schema \"cypher.test\" 1\n{}";
        source_fixture_t fixture{};
        key_value_source_options_t options = SourceOptions( fixture );
        options.cbMaxAggregateSources = StringView_FromCString( pRoot ).cchLength - 1u;
        key_value_document_t *pDocument = CreateDocument();
        const key_value_source_result_t result = ParseSource(
            "data/root.cydf",
            pRoot,
            fixture,
            pDocument,
            &options );
        CHECK( result.status == key_value_source_status_t::SOURCE_BYTE_LIMIT );
        CHECK( result.nUniqueSources == 0u );
        KeyValue_DestroyDocument( pDocument );
    }

    SECTION( "dependency depth limit" ) {
        constexpr const char *pMiddle =
            "@cykv 2\n@schema \"cypher.shared\" 1\n"
            "#include \"b.cydf\" as b\n{}";
        constexpr const char *pRoot =
            "@cykv 2\n@schema \"cypher.test\" 1\n"
            "#include \"a.cydf\" as a\n{}";
        const source_entry_t depthEntries[]{
            { "a.cydf", "data/a.cydf", pMiddle },
            { "b.cydf", "data/b.cydf", pDependency }
        };
        source_fixture_t fixture{ depthEntries, 2u };
        key_value_source_options_t options = SourceOptions( fixture );
        options.nMaxDependencyDepth = 1u;
        key_value_document_t *pDocument = CreateDocument();
        const key_value_source_result_t result = ParseSource(
            "data/root.cydf",
            pRoot,
            fixture,
            pDocument,
            &options );
        CHECK( result.status ==
               key_value_source_status_t::DEPENDENCY_DEPTH_LIMIT );
        CHECK( fixture.nOpened == fixture.nReleased );
        KeyValue_DestroyDocument( pDocument );
    }

    SECTION( "hard graph ceilings are accepted exactly" ) {
        constexpr const char *pRoot =
            "@cykv 2\n@schema \"cypher.test\" 1\n{}";
        source_fixture_t fixture{};
        key_value_source_options_t options = SourceOptions( fixture );
        options.nMaxDependencyDepth =
            CY_KEY_VALUE_SOURCE_MAX_DEPENDENCY_DEPTH;
        options.nMaxSources = CY_KEY_VALUE_SOURCE_MAX_SOURCES;
        options.nMaxEdges = CY_KEY_VALUE_SOURCE_MAX_EDGES;
        key_value_document_t *pDocument = CreateDocument();
        CHECK( ParseSource(
            "data/root.cydf",
            pRoot,
            fixture,
            pDocument,
            &options ).status == key_value_source_status_t::OK );
        CHECK( fixture.nOpened == 0u );
        KeyValue_DestroyDocument( pDocument );
    }

    SECTION( "dependency depth above the hard ceiling is rejected" ) {
        source_fixture_t fixture{};
        key_value_source_options_t options = SourceOptions( fixture );
        options.nMaxDependencyDepth =
            CY_KEY_VALUE_SOURCE_MAX_DEPENDENCY_DEPTH + 1u;
        key_value_document_t *pDocument = CreateDocument();
        CHECK( ParseSource(
            "data/root.cydf",
            pDependency,
            fixture,
            pDocument,
            &options ).status == key_value_source_status_t::INVALID_ARGUMENT );
        CHECK( fixture.nOpened == 0u );
        KeyValue_DestroyDocument( pDocument );
    }

    SECTION( "source count above the hard ceiling is rejected" ) {
        source_fixture_t fixture{};
        key_value_source_options_t options = SourceOptions( fixture );
        options.nMaxSources = CY_KEY_VALUE_SOURCE_MAX_SOURCES + 1u;
        key_value_document_t *pDocument = CreateDocument();
        CHECK( ParseSource(
            "data/root.cydf",
            pDependency,
            fixture,
            pDocument,
            &options ).status == key_value_source_status_t::INVALID_ARGUMENT );
        CHECK( fixture.nOpened == 0u );
        KeyValue_DestroyDocument( pDocument );
    }

    SECTION( "edge count above the hard ceiling is rejected" ) {
        source_fixture_t fixture{};
        key_value_source_options_t options = SourceOptions( fixture );
        options.nMaxEdges = CY_KEY_VALUE_SOURCE_MAX_EDGES + 1u;
        key_value_document_t *pDocument = CreateDocument();
        CHECK( ParseSource(
            "data/root.cydf",
            pDependency,
            fixture,
            pDocument,
            &options ).status == key_value_source_status_t::INVALID_ARGUMENT );
        CHECK( fixture.nOpened == 0u );
        KeyValue_DestroyDocument( pDocument );
    }

    SECTION( "overflow-scale edge count is rejected before allocation" ) {
        source_fixture_t fixture{};
        key_value_source_options_t options = SourceOptions( fixture );
        options.nMaxEdges = CY_USIZE_MAX;
        key_value_document_t *pDocument = CreateDocument();
        CHECK( ParseSource(
            "data/root.cydf",
            pDependency,
            fixture,
            pDocument,
            &options ).status == key_value_source_status_t::INVALID_ARGUMENT );
        CHECK( fixture.nOpened == 0u );
        KeyValue_DestroyDocument( pDocument );
    }

    SECTION( "overflow-scale source count is rejected before allocation" ) {
        source_fixture_t fixture{};
        key_value_source_options_t options = SourceOptions( fixture );
        options.nMaxSources = CY_USIZE_MAX;
        key_value_document_t *pDocument = CreateDocument();
        CHECK( ParseSource(
            "data/root.cydf",
            pDependency,
            fixture,
            pDocument,
            &options ).status == key_value_source_status_t::INVALID_ARGUMENT );
        CHECK( fixture.nOpened == 0u );
        KeyValue_DestroyDocument( pDocument );
    }

    SECTION( "dependency directives cannot follow definitions" ) {
        constexpr const char *pRoot =
            "@cykv 2\n@schema \"cypher.test\" 1\n"
            "#define LOCAL 1\n#include \"a.cydf\" as a\n"
            "{ value = $LOCAL }";
        source_fixture_t fixture{ entries, 2u };
        key_value_document_t *pDocument = CreateDocument();
        const key_value_source_result_t result = ParseSource(
            "data/root.cydf",
            pRoot,
            fixture,
            pDocument );
        CHECK( result.status == key_value_source_status_t::PARSE_FAILED );
        CHECK( result.parseResult.status == key_value_parse_status_t::SYNTAX_ERROR );
        CHECK( fixture.nOpened == 0u );
        KeyValue_DestroyDocument( pDocument );
    }

    SECTION( "CYKV 1 never resolves directives" ) {
        constexpr const char *pRoot =
            "@cykv 1\n@schema \"cypher.test\" 1\n"
            "#include \"a.cydf\" as a\n{}";
        source_fixture_t fixture{ entries, 2u };
        key_value_document_t *pDocument = CreateDocument();
        const key_value_source_result_t result = ParseSource(
            "data/root.cydf",
            pRoot,
            fixture,
            pDocument );
        CHECK( result.status == key_value_source_status_t::PARSE_FAILED );
        CHECK( result.parseResult.status == key_value_parse_status_t::SYNTAX_ERROR );
        CHECK( fixture.nOpened == 0u );
        KeyValue_DestroyDocument( pDocument );
    }

    SECTION( "invalid include syntax" ) {
        // Without `as` an include merges a part; a dangling `as` is an error.
        constexpr const char *pRoot =
            "@cykv 2\n@schema \"cypher.test\" 1\n"
            "#include \"a.cydf\" as\n{}";
        source_fixture_t fixture{ entries, 2u };
        key_value_document_t *pDocument = CreateDocument();
        CHECK( ParseSource(
            "data/root.cydf",
            pRoot,
            fixture,
            pDocument ).status == key_value_source_status_t::INVALID_DIRECTIVE );
        KeyValue_DestroyDocument( pDocument );
    }

    SECTION( "header probe does not charge synthetic root bytes to input" ) {
        constexpr const char *pRoot =
            "@cykv 2\n@schema \"cypher.test\" 1\n";
        source_fixture_t fixture{};
        key_value_source_options_t options = SourceOptions( fixture );
        options.parseOptions.cbMaxInput =
            StringView_FromCString( pRoot ).cchLength;
        key_value_document_t *pDocument = CreateDocument();
        const key_value_source_result_t result = ParseSource(
            "data/root.cydf",
            pRoot,
            fixture,
            pDocument,
            &options );
        CHECK( result.status == key_value_source_status_t::PARSE_FAILED );
        CHECK( result.parseResult.status ==
               key_value_parse_status_t::SYNTAX_ERROR );
        CHECK( fixture.nOpened == 0u );
        KeyValue_DestroyDocument( pDocument );
    }

    SECTION( "failure preserves destination" ) {
        constexpr const char *pRoot =
            "@cykv 2\n@schema \"cypher.test\" 1\n"
            "#base \"missing.cydf\"\n{}";
        source_fixture_t fixture{};
        key_value_document_t *pDocument = CreateDocument();
        REQUIRE( KeyValue_SetDocumentHeader(
            pDocument,
            {
                CYKV_LANGUAGE_VERSION_1,
                StringView_FromCString( "cypher.stable" ),
                9u
            } ) );
        REQUIRE( KeyValue_SetRootType(
            pDocument,
            key_value_type_t::OBJECT ) );
        key_value_t *pStable = KeyValue_ObjectInsert(
            pDocument,
            KeyValue_Root( pDocument ),
            StringView_FromCString( "stable" ),
            key_value_type_t::BOOL );
        REQUIRE( pStable != nullptr );
        REQUIRE( KeyValue_SetBool( pDocument, pStable, CY_TRUE ) );

        CHECK( ParseSource(
            "data/root.cydf",
            pRoot,
            fixture,
            pDocument ).status == key_value_source_status_t::OPEN_FAILED );
        CHECK( KeyValue_DocumentHeader( pDocument ).nSchemaVersion == 9u );
        bool_t stable = CY_FALSE;
        REQUIRE( KeyValue_GetBool(
            FindRequired( KeyValue_Root( pDocument ), "stable" ),
            &stable ) );
        CHECK( stable );
        KeyValue_DestroyDocument( pDocument );
    }

    CHECK( StringView_Equals(
        StringView_FromCString( KeyValue_SourceStatusName(
            key_value_source_status_t::DEPENDENCY_CYCLE ) ),
        StringView_FromCString( "DEPENDENCY_CYCLE" ) ) );
}

// ---------------------------------------------------------------------------
// Merging includes, folder includes, and operators against #base
// ---------------------------------------------------------------------------

namespace
{

// A folder listing for the list callback: names in deliberately unsorted
// order, including ones the pattern must leave out.
struct folder_fixture_t {
    source_fixture_t files{};
    const char *pFolder{ nullptr };
    const char *const *ppNames{ nullptr };
    usize nNames{ 0u };
    usize nListed{ 0u };
};

key_value_source_open_status_t OpenFolderSource(
    string_view_t requestingSourcePath,
    string_view_t referencedPath,
    key_value_source_view_t *pOutSource,
    void *pUserData ) noexcept
{
    return OpenSource( requestingSourcePath, referencedPath, pOutSource, &static_cast<folder_fixture_t *>( pUserData )->files );
}

void ReleaseFolderSource( const key_value_source_view_t *pSource, void *pUserData ) noexcept
{
    ReleaseSource( pSource, &static_cast<folder_fixture_t *>( pUserData )->files );
}

key_value_source_open_status_t ListFolder(
    string_view_t,
    string_view_t referencedFolder,
    key_value_source_emit_fn_t pfnEmit,
    void *pEmitContext,
    void *pUserData ) noexcept
{
    auto &fixture = *static_cast<folder_fixture_t *>( pUserData );
    ++fixture.nListed;
    if ( !StringView_Equals( referencedFolder, StringView_FromCString( fixture.pFolder ) ) ) {
        return key_value_source_open_status_t::NOT_FOUND;
    }
    for ( usize i = 0u; i < fixture.nNames; ++i ) {
        if ( !pfnEmit( pEmitContext, StringView_FromCString( fixture.ppNames[i] ) ) ) { break; }
    }
    return key_value_source_open_status_t::OK;
}

key_value_source_options_t FolderOptions( folder_fixture_t &fixture )
{
    key_value_source_options_t options{};
    options.pfnOpen = OpenFolderSource;
    options.pfnRelease = ReleaseFolderSource;
    options.pfnList = ListFolder;
    options.pUserData = &fixture;
    return options;
}

key_value_dependency_kind_t g_lastKind = key_value_dependency_kind_t::INCLUDE;

bool_t RecordKind( string_view_t, string_view_t, key_value_dependency_kind_t kind, void * ) noexcept
{
    g_lastKind = kind;
    return CY_TRUE;
}

} // namespace

TEST_CASE( "CYKV 2 merging includes add parts and refuse collisions",
           "[CypherCommon][Tier1][CYKV][Source][Include]" )
{
    constexpr const char *pPartA = "@cykv 2\n@schema \"cypher.game\" 1\n{ classes = { light = { solid = false } } settings = { gravity = 800 } }";
    constexpr const char *pPartB = "@cykv 2\n@schema \"cypher.game\" 1\n{ classes = { door = { solid = true } } version = 3 }";
    constexpr const char *pRoot =
        "@cykv 2\n@schema \"cypher.game\" 1\n"
        "#include \"parts/entities_a.cydf\"\n"
        "#include \"parts/entities_b.cydf\" // second part\n"
        "{ name = \"reap\" classes = { worldspawn = { solid = true } } }";
    const source_entry_t entries[]{
        { "parts/entities_a.cydf", "game/parts/entities_a.cydf", pPartA },
        { "parts/entities_b.cydf", "game/parts/entities_b.cydf", pPartB }
    };

    SECTION( "parts merge into the document" ) {
        source_fixture_t fixture{ entries, 2u };
        key_value_source_options_t options = SourceOptions( fixture );
        options.pfnDependency = RecordKind;
        key_value_document_t *pDocument = CreateDocument();
        const key_value_source_result_t result = ParseSource( "game/reap.cygame", pRoot, fixture, pDocument, &options );
        REQUIRE( result.status == key_value_source_status_t::OK );
        CHECK( g_lastKind == key_value_dependency_kind_t::MERGE_INCLUDE );
        key_value_t *pClasses = FindRequired( KeyValue_Root( pDocument ), "classes" );
        CHECK( KeyValue_ChildCount( pClasses ) == 3u ); // worldspawn, light, door.
        CHECK( KeyValue_Find( pClasses, StringView_FromCString( "door" ) ) != nullptr );
        CHECK( KeyValue_Find( KeyValue_Root( pDocument ), StringView_FromCString( "settings" ) ) != nullptr );
        CHECK( KeyValue_Find( KeyValue_Root( pDocument ), StringView_FromCString( "version" ) ) != nullptr );
        KeyValue_DestroyDocument( pDocument );
    }

    SECTION( "a member two parts both define is an error naming the part" ) {
        constexpr const char *pClash = "@cykv 2\n@schema \"cypher.game\" 1\n{ name = \"other\" }";
        const source_entry_t clashing[]{
            { "parts/entities_a.cydf", "game/parts/entities_a.cydf", pPartA },
            { "parts/entities_b.cydf", "game/parts/entities_b.cydf", pClash }
        };
        source_fixture_t fixture{ clashing, 2u };
        key_value_document_t *pDocument = CreateDocument();
        const key_value_source_result_t result = ParseSource( "game/reap.cygame", pRoot, fixture, pDocument );
        CHECK( result.status == key_value_source_status_t::INCLUDE_CONFLICT );
        CHECK( std::string( result.errorReferencedPath ) == "parts/entities_b.cydf" );
        CHECK( result.dependencyKind == key_value_dependency_kind_t::MERGE_INCLUDE );
        KeyValue_DestroyDocument( pDocument );
    }

    SECTION( "a part must be the same kind of document" ) {
        constexpr const char *pOther = "@cykv 2\n@schema \"cypher.settings\" 1\n{ version = 3 }";
        const source_entry_t foreign[]{
            { "parts/entities_a.cydf", "game/parts/entities_a.cydf", pPartA },
            { "parts/entities_b.cydf", "game/parts/entities_b.cydf", pOther }
        };
        source_fixture_t fixture{ foreign, 2u };
        key_value_document_t *pDocument = CreateDocument();
        CHECK( ParseSource( "game/reap.cygame", pRoot, fixture, pDocument ).status == key_value_source_status_t::SCHEMA_MISMATCH );
        KeyValue_DestroyDocument( pDocument );
    }
}

TEST_CASE( "CYKV 2 folder includes take every matching file in sorted order",
           "[CypherCommon][Tier1][CYKV][Source][Include]" )
{
    constexpr const char *pRifle = "@cykv 2\n@schema \"cypher.weapon\" 1\n{ damage = 24 }";
    constexpr const char *pSmg = "@cykv 2\n@schema \"cypher.weapon\" 1\n{ damage = 12 }";
    const source_entry_t entries[]{
        { "weapons/rifle.cydf", "data/weapons/rifle.cydf", pRifle },
        { "weapons/smg.cydf", "data/weapons/smg.cydf", pSmg }
    };
    const char *const names[]{ "smg.cydf", "notes.txt", ".hidden.cydf", "rifle.cydf" };

    SECTION( "a namespaced folder include binds an object keyed by file stem" ) {
        folder_fixture_t fixture{ { entries, 2u }, "weapons", names, 4u };
        key_value_source_options_t options = FolderOptions( fixture );
        constexpr const char *pRoot =
            "@cykv 2\n@schema \"cypher.arsenal\" 1\n"
            "#include \"weapons/*.cydf\" as weapons\n"
            "{ arsenal = $weapons first = $weapons.rifle.damage }";
        key_value_document_t *pDocument = CreateDocument();
        const key_value_source_result_t result = KeyValue_ParseSource(
            StringView_FromCString( "data/arsenal.cydf" ), StringView_FromCString( pRoot ), options, pDocument );
        REQUIRE( result.status == key_value_source_status_t::OK );
        CHECK( result.nDependencyEdges == 2u );
        key_value_t *pArsenal = FindRequired( KeyValue_Root( pDocument ), "arsenal" );
        REQUIRE( KeyValue_ChildCount( pArsenal ) == 2u );
        CHECK( StringView_Equals( KeyValue_Name( KeyValue_ChildAt( pArsenal, 0u ) ), StringView_FromCString( "rifle" ) ) );
        CHECK( StringView_Equals( KeyValue_Name( KeyValue_ChildAt( pArsenal, 1u ) ), StringView_FromCString( "smg" ) ) );
        i64 damage = 0;
        REQUIRE( KeyValue_GetI64( FindRequired( KeyValue_Root( pDocument ), "first" ), &damage ) );
        CHECK( damage == 24 );
        KeyValue_DestroyDocument( pDocument );
    }

    SECTION( "a merging folder include merges every file" ) {
        constexpr const char *pA = "@cykv 2\n@schema \"cypher.arsenal\" 1\n{ rifle = { damage = 24 } }";
        constexpr const char *pB = "@cykv 2\n@schema \"cypher.arsenal\" 1\n{ smg = { damage = 12 } }";
        const source_entry_t parts[]{
            { "weapons/rifle.cydf", "data/weapons/rifle.cydf", pA },
            { "weapons/smg.cydf", "data/weapons/smg.cydf", pB }
        };
        folder_fixture_t fixture{ { parts, 2u }, "weapons", names, 4u };
        key_value_source_options_t options = FolderOptions( fixture );
        constexpr const char *pRoot = "@cykv 2\n@schema \"cypher.arsenal\" 1\n#include \"weapons/*.cydf\"\n{}";
        key_value_document_t *pDocument = CreateDocument();
        REQUIRE( KeyValue_ParseSource( StringView_FromCString( "data/arsenal.cydf" ), StringView_FromCString( pRoot ), options, pDocument ).status ==
                 key_value_source_status_t::OK );
        CHECK( KeyValue_ChildCount( KeyValue_Root( pDocument ) ) == 2u );
        KeyValue_DestroyDocument( pDocument );
    }

    SECTION( "no match, no list callback, and a wildcard folder are errors" ) {
        folder_fixture_t fixture{ { entries, 2u }, "weapons", names, 4u };
        key_value_source_options_t options = FolderOptions( fixture );
        key_value_document_t *pDocument = CreateDocument();
        const auto status = [&]( const char *pRoot ) {
            return KeyValue_ParseSource( StringView_FromCString( "data/arsenal.cydf" ), StringView_FromCString( pRoot ), options, pDocument ).status;
        };
        CHECK( status( "@cykv 2\n@schema \"cypher.arsenal\" 1\n#include \"weapons/*.cykv\" as weapons\n{}" ) == key_value_source_status_t::OPEN_FAILED );
        CHECK( status( "@cykv 2\n@schema \"cypher.arsenal\" 1\n#include \"armor/*.cydf\" as armor\n{}" ) == key_value_source_status_t::OPEN_FAILED );
        CHECK( status( "@cykv 2\n@schema \"cypher.arsenal\" 1\n#include \"*/rifle.cydf\" as weapons\n{}" ) == key_value_source_status_t::INVALID_PATH );
        CHECK( status( "@cykv 2\n@schema \"cypher.arsenal\" 1\n#base \"weapons/*.cydf\"\n{}" ) == key_value_source_status_t::INVALID_PATH );
        options.pfnList = nullptr;
        CHECK( status( "@cykv 2\n@schema \"cypher.arsenal\" 1\n#include \"weapons/*.cydf\" as weapons\n{}" ) == key_value_source_status_t::INVALID_ARGUMENT );
        KeyValue_DestroyDocument( pDocument );
    }
}

TEST_CASE( "CYKV 2 #delete and += patch a #base document",
           "[CypherCommon][Tier1][CYKV][Source][Base]" )
{
    constexpr const char *pBase =
        "@cykv 2\n@schema \"cypher.mode\" 1\n"
        "{ weapons = { rifle = { damage = 24 scope = true tags = [\"a\"] } } lives = 3 }";
    const source_entry_t entries[]{ { "base.cydf", "data/base.cydf", pBase } };
    const auto parse = [&]( const char *pRoot, key_value_document_t *pDocument ) {
        source_fixture_t fixture{ entries, 1u };
        return ParseSource( "data/mod.cydf", pRoot, fixture, pDocument );
    };

    key_value_document_t *pDocument = CreateDocument();
    const key_value_source_result_t result = parse(
        "@cykv 2\n@schema \"cypher.mode\" 1\n#base \"base.cydf\"\n"
        "{ weapons = { rifle = { damage = 30 scope = #delete tags += [\"b\"] } } }", pDocument );
    REQUIRE( result.status == key_value_source_status_t::OK );
    key_value_t *pRifle = FindRequired( FindRequired( KeyValue_Root( pDocument ), "weapons" ), "rifle" );
    i64 damage = 0;
    REQUIRE( KeyValue_GetI64( FindRequired( pRifle, "damage" ), &damage ) );
    CHECK( damage == 30 );
    CHECK( KeyValue_Find( pRifle, StringView_FromCString( "scope" ) ) == nullptr );
    CHECK( KeyValue_ChildCount( FindRequired( pRifle, "tags" ) ) == 2u );
    CHECK( KeyValue_Find( KeyValue_Root( pDocument ), StringView_FromCString( "lives" ) ) != nullptr );
    KeyValue_DestroyDocument( pDocument );

    key_value_document_t *pMissing = CreateDocument();
    const key_value_source_result_t missing = parse(
        "@cykv 2\n@schema \"cypher.mode\" 1\n#base \"base.cydf\"\n{ armour = #delete }", pMissing );
    CHECK( missing.status == key_value_source_status_t::PARSE_FAILED );
    CHECK( missing.parseResult.status == key_value_parse_status_t::INHERITED_VALUE_MISSING );
    KeyValue_DestroyDocument( pMissing );
}

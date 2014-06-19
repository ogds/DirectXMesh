//-------------------------------------------------------------------------------------
// DirectXMeshValidate.cpp
//  
// DirectX Mesh Geometry Library - Mesh validation
//
// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved.
//
// http://go.microsoft.com/fwlink/?LinkID=324981
//-------------------------------------------------------------------------------------

#include "DirectXMeshP.h"

using namespace DirectX;

namespace
{

//-------------------------------------------------------------------------------------
// Validates indices and optionally the adjacency information
//-------------------------------------------------------------------------------------
template<class index_t>
HRESULT ValidateIndices( _In_reads_(nFaces*3) const index_t* indices, _In_ size_t nFaces,
                         _In_ size_t nVerts, _In_reads_opt_(nFaces*3) const uint32_t* adjacency,
                         _In_ DWORD flags, _In_opt_ std::wstring* msgs )
{
    if ( ( flags & VALIDATE_BACKFACING ) && !adjacency )
    {
        if ( msgs )
            *msgs += L"Missing adjacency information required to check for BACKFACING\n";

        return E_INVALIDARG;
    }

    bool result = true;

    for( size_t face = 0; face < nFaces; ++face )
    {
        for( size_t point = 0; point < 3; ++point )
        {
            index_t i = indices[ face*3 + point ];
            if ( i >= nVerts && i != index_t(-1) )
            {
                if ( !msgs )
                    return E_FAIL;

                result = false;

                wchar_t buff[ 128 ];
                swprintf_s( buff, L"An invalid index value (%u) was found on face %Iu\n", i, face );
                *msgs += buff;
            }

            if ( adjacency )
            {
                uint32_t j = adjacency[ face*3 + point ];
                if ( j >= nFaces && j != UNUSED32 )
                {
                    if ( !msgs )
                        return E_FAIL;

                    result = false;

                    wchar_t buff[ 128 ];
                    swprintf_s( buff, L"An invalid neighbor index value (%u) was found on face %Iu\n", j, face );
                    *msgs += buff;
                }
            }
        }

        // Check for degenerate triangles
        index_t i0 = indices[ face*3 ];
        index_t i1 = indices[ face*3 + 1 ];
        index_t i2 = indices[ face*3 + 2 ];

        if ( i0 == i1
             || i0 == i2
             || i1 == i2 )
        {
            if ( flags & VALIDATE_DEGENERATE )
            {
                if ( !msgs )
                    return E_FAIL;

                result = false;

                index_t bad;
                if ( i0 == i1 )
                    bad = i0;
                else if ( i1 == i2 )
                    bad = i2;
                else
                    bad = i0;

                wchar_t buff[ 128 ];
                swprintf_s( buff, L"A point (%u) was found more than once in triangle %Iu\n", bad, face );
                *msgs += buff;
            }

            continue;
        }

        // Check for duplicate neighbor
        if ( ( flags & VALIDATE_BACKFACING ) && adjacency )
        {
            uint32_t j0 = adjacency[ face*3 ];
            uint32_t j1 = adjacency[ face*3 + 1 ];
            uint32_t j2 = adjacency[ face*3 + 2 ];

            if ( ( j0 == j1 && j0 != UNUSED32 )
                    || ( j0 == j2 && j0 != UNUSED32 )
                    || ( j1 == j2 && j1 != UNUSED32 ) )
            {
                if ( !msgs )
                    return E_FAIL;

                result = false;

                uint32_t bad;
                if ( j0 == j1 && j0 != UNUSED32 )
                    bad = j0;
                else if ( j0 == j2 && j0 != UNUSED32 )
                    bad = j0;
                else
                    bad = j1;

                wchar_t buff[ 256 ];
                swprintf_s( buff, L"A neighbor triangle (%u) was found more than once on triangle %Iu\n"
                                  L"\t(likley problem is that two triangles share same points with opposite direction)\n", bad, face );
                *msgs += buff;
            }
        }
    }

    return result ? S_OK : E_FAIL;
}


//-------------------------------------------------------------------------------------
// Validates mesh contains no bowties (i.e. a vertex is the apex of two separate triangle fans)
//-------------------------------------------------------------------------------------
template<class index_t>
HRESULT ValidateNoBowties( _In_reads_(nFaces*3) const index_t* indices, _In_ size_t nFaces,
                           _In_ size_t nVerts, _In_reads_opt_(nFaces*3) const uint32_t* adjacency,
                           _In_opt_ std::wstring* msgs )
{
    if ( !adjacency )
    {
        if ( msgs )
            *msgs += L"Missing adjacency information required to check for BOWTIES\n";
            
        return E_INVALIDARG;
    }

    size_t tsize = ( sizeof(bool) * nFaces * 3 ) + ( sizeof(index_t) * nVerts * 2 ) + ( sizeof(bool) * nVerts );
    std::unique_ptr<uint8_t[]> temp( new (std::nothrow) uint8_t[ tsize ] );
    if ( !temp )
        return E_OUTOFMEMORY;

    auto faceSeen = reinterpret_cast<bool*>( temp.get() );
    auto faceIds = reinterpret_cast<index_t*>( temp.get() + sizeof(bool) * nFaces * 3 );
    auto faceUsing = reinterpret_cast<index_t*>( reinterpret_cast<uint8_t*>( faceIds ) + sizeof(index_t) * nVerts );
    auto vertexBowtie = reinterpret_cast<bool*>( reinterpret_cast<uint8_t*>( faceUsing ) + sizeof(index_t) * nVerts );

    memset( faceSeen, 0, sizeof(bool) * nFaces * 3 );
    memset( faceIds, 0xFF, sizeof(index_t) * nVerts );
    memset( faceUsing, 0, sizeof(index_t) * nVerts );
    memset( vertexBowtie, 0, sizeof(bool) * nVerts );

    orbit_iterator<index_t> ovi( adjacency, indices, nFaces );

    bool result = true;

    for( uint32_t face = 0; face < nFaces; ++face )
    {
        index_t i0 = indices[ face*3 ];
        index_t i1 = indices[ face*3 + 1 ];
        index_t i2 = indices[ face*3 + 2 ];

        if ( i0 == i1
             || i0 == i2
             || i1 == i2 )
        {
            // ignore degenerate faces
            faceSeen[ face * 3 ] = true;
            faceSeen[ face * 3 + 1 ] = true;
            faceSeen[ face * 3 + 2 ] = true;
            continue;
        }

        for( size_t point = 0; point < 3; ++point )
        {
            if ( faceSeen[ face * 3 + point ] )
                continue;

            faceSeen[ face * 3 + point ] = true;

            index_t i = indices[ face*3 + point ];
            ovi.initialize( face, i, orbit_iterator<index_t>::ALL );
            ovi.moveToCCW();
            while ( !ovi.done() )
            {
                uint32_t curFace = ovi.nextFace();
                if ( curFace >= nFaces )
                    return E_FAIL;

                uint32_t curPoint = ovi.getpoint();
                if ( curPoint > 2 )
                    return E_FAIL;

                faceSeen[ curFace*3 + curPoint ] = true;

                uint32_t j = indices[ curFace * 3 + curPoint ];

                if ( faceIds[ j ] == index_t(-1) )
                {
                    faceIds[ j ] = index_t( face );
                    faceUsing[ j ] = index_t( curFace );
                }
                else if ( ( faceIds[ j ] != index_t( face ) ) && !vertexBowtie[ j ] )
                {
                    // We found a (unique) bowtie!

                    if ( !msgs )
                        return E_FAIL;

                    if ( result )
                    {
                        // If this is the first bowtie found, add a quick explanation
                        *msgs += L"A bowtie was found.  Bowties can be fixed by calling Clean\n"
                                 L"  A bowtie is the usage of a single vertex by two separate fans of triangles.\n"
                                 L"  The fix is to duplicate the vertex so that each fan has its own vertex.\n";
                        result = false;
                    }

                    vertexBowtie[ j ] = true;

                    wchar_t buff[ 256 ];
                    swprintf_s( buff, L"\nBowtie found around vertex %u shared by faces %u and %u\n", j, curFace, faceUsing[ j ] );
                    *msgs += buff;
                }
            }
        }
    }

    return result ? S_OK : E_FAIL;
}

};

namespace DirectX
{

//=====================================================================================
// Entry-points
//=====================================================================================

//-------------------------------------------------------------------------------------
_Use_decl_annotations_
HRESULT Validate( const uint16_t* indices, size_t nFaces, size_t nVerts,
                  const uint32_t* adjacency, DWORD flags, std::wstring* msgs )
{
    if ( !indices || !nFaces || !nVerts )
        return E_INVALIDARG;

    if ( ( uint64_t(nFaces) * 3 ) > 0xFFFFFFFF )
        return HRESULT_FROM_WIN32( ERROR_ARITHMETIC_OVERFLOW );

    if ( msgs )
        msgs->clear();

    HRESULT hr = ValidateIndices<uint16_t>( indices, nFaces, nVerts, adjacency, flags, msgs );
    if ( FAILED(hr) )
        return hr;

    if ( flags & VALIDATE_BOWTIES )
    {
        hr = ValidateNoBowties<uint16_t>( indices, nFaces, nVerts, adjacency, msgs );
        if ( FAILED(hr) )
            return hr;
    }

    return S_OK;
}


//-------------------------------------------------------------------------------------
_Use_decl_annotations_
HRESULT Validate( const uint32_t* indices, size_t nFaces, size_t nVerts,
                  const uint32_t* adjacency, DWORD flags, std::wstring* msgs )
{
    if ( !indices || !nFaces || !nVerts )
        return E_INVALIDARG;

    if ( ( uint64_t(nFaces) * 3 ) > 0xFFFFFFFF )
        return HRESULT_FROM_WIN32( ERROR_ARITHMETIC_OVERFLOW );

    if ( msgs )
        msgs->clear();

    HRESULT hr = ValidateIndices<uint32_t>( indices, nFaces, nVerts, adjacency, flags, msgs );
    if ( FAILED(hr) )
        return hr;

    if ( flags & VALIDATE_BOWTIES )
    {
        hr = ValidateNoBowties<uint32_t>( indices, nFaces, nVerts, adjacency, msgs );
        if ( FAILED(hr) )
            return hr;
    }

    return S_OK;
}

} // namespace
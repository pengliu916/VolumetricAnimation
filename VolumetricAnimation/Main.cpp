#include "stdafx.h"
#include "VolumetricAnimation.h"

_Use_decl_annotations_
int WINAPI WinMain( HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow )
{
	VolumetricAnimation sample( 800, 600, L"D3D12 Volumetric Animation" );
	return sample.Run( hInstance, nCmdShow );
}

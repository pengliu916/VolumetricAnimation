#include "stdafx.h"
#include "RotatingCube.h"

_Use_decl_annotations_
int WINAPI WinMain( HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow )
{
	RotatingCube sample( 1280, 720, L"D3D12 Rotating Cube" );
	return sample.Run( hInstance, nCmdShow );
}

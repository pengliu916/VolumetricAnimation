#pragma once
// To use this framework, create new project in this solution
// 1. right click new project's References and add UtilityLibrary as reference
// 2. open project property and open configuration manager delete all 
//    Win32 configurations and for each configuration only build 
//    UtilityLibrary and the new project not any others
// 3. go to Configuration Properties -> General, change Target Platform
//    Version to 10.0+
// 4. go to C/C++ -> General, add '../UtilityLibrary' to Include Directories
// 5. go to C/C++ -> Preprocessor add '_CRT_SECURE_NO_WARNINGS;NDEBUG;_NDEBUG;'
//    to Release and '_CRT_SECURE_NO_WARNINGS;_DEBUG;DEBUG;' to Debug
// 6. go to C/C++ -> Precompiled Headers change Precompiled Header to 'Use'
//    and for stdafx.cpp change it to 'Create'
// 7. go to Linker -> Input, add 'd3d12.lib;dxgi.lib;d3dcompiler.lib;dxguid.lib;'
// 8. for .hlsl files change it item type to Custom Build Tool, and change the 
//    Content attribute to Yes
// 9. go to hlsl files' Configuration Properties -> Custom Build Tool -> General
//    add Command Line 'copy %(Identity) "$(OutDir)" >NUL'
//    add Outputs '$(OutDir)\%(Identity)' and Treat Output As Content 'Yes'

#include "DXHelper.h"

class DX12Framework
{
public:
	DX12Framework(UINT width, UINT height, std::wstring name);
	virtual ~DX12Framework();

	int Run(HINSTANCE hInstance, int nCmdShow);
	void SetCustomWindowText(LPCWSTR text);

protected:

	void RenderLoop();
	
	virtual HRESULT OnInit() = 0;
	virtual HRESULT OnSizeChanged() = 0;
	virtual void OnUpdate() = 0;
	virtual void OnRender() = 0;
	virtual void OnDestroy() = 0;
	virtual bool OnEvent(MSG msg) = 0;

	std::wstring GetAssetFullPath(LPCWSTR assetName);

	void GetHardwareAdapter( _In_ IDXGIFactory4* pFactory, _Outptr_result_maybenull_ IDXGIAdapter1** ppAdapter );

	static LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

	bool _stopped;
	bool _error;
	// In multi-thread scenario, current thread may read old version of the following boolean due to 
	// unflushed cache etc. So to use flag in multi-thread cases, atomic bool is needed, and memory order semantic is crucial 
	std::atomic<bool> _resize;

	// Viewport dimensions.
	UINT m_width;
	UINT m_height;
	float m_aspectRatio;
	
	UINT m_newWidth;
	UINT m_newHeight;
	// Window handle.
	HWND m_hwnd;

	// Adapter info.
	bool m_useWarpDevice;

private:
	void ParseCommandLineArgs();

	// Root assets path.
	std::wstring m_assetsPath;

	// Window title.
	std::wstring m_title;

};

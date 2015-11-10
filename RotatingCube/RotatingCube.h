#pragma once

#include "DX12Framework.h"
#include "Camera.h"
#include "StepTimer.h"

using namespace DirectX;
using namespace Microsoft::WRL;

class RotatingCube : public DX12Framework
{
public:
	RotatingCube( UINT width, UINT height, std::wstring name );

protected:
	virtual HRESULT OnInit();
	virtual HRESULT OnSizeChanged();
	virtual void OnUpdate();
	virtual void OnRender();
	virtual void OnDestroy();
	virtual bool OnEvent( MSG msg );

private:
	static const UINT FrameCount = 3;

	struct Vertex
	{
		XMFLOAT3 position;
		XMFLOAT3 color;
	};

	// Pipeline objects.
	D3D12_VIEWPORT m_viewport;
	D3D12_RECT m_scissorRect;
	ComPtr<IDXGISwapChain3> m_swapChain;
	ComPtr<ID3D12Device> m_device;
	ComPtr<ID3D12Resource> m_renderTargets[FrameCount];
	ComPtr<ID3D12CommandAllocator> m_commandAllocator;
	ComPtr<ID3D12CommandQueue> m_commandQueue;
	ComPtr<ID3D12RootSignature> m_rootSignature;
	ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
	ComPtr<ID3D12DescriptorHeap> m_dsvHeap;
	ComPtr<ID3D12PipelineState> m_pipelineState;
	ComPtr<ID3D12GraphicsCommandList> m_commandList;
	UINT m_rtvDescriptorSize;

	// App resources.
	ComPtr<ID3D12Resource> m_depthBuffer;
	ComPtr<ID3D12Resource> m_constantBuffer;
	ComPtr<ID3D12Resource> m_vertexBuffer;
	ComPtr<ID3D12Resource> m_indexBuffer;
	D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView;
	D3D12_INDEX_BUFFER_VIEW m_indexBufferView;
	CModelViewerCamera m_camera;
	StepTimer m_timer;

	// Synchronization objects.
	UINT m_frameIndex;
	HANDLE m_fenceEvent;
	ComPtr<ID3D12Fence> m_fence;
	UINT64 m_fenceValue;

	HRESULT LoadPipeline();
	HRESULT LoadAssets();
	HRESULT LoadSizeDependentResource();
	void PopulateCommandList();
	void WaitForPreviousFrame();
};

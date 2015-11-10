#pragma once

#include "DX12Framework.h"
#include "Camera.h"
#include "StepTimer.h"

using namespace DirectX;
using namespace Microsoft::WRL;

#define CBUFFER_ALIGN __declspec(align(D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT))
class VolumetricAnimation : public DX12Framework
{
public:
	VolumetricAnimation( UINT width, UINT height, std::wstring name );

protected:
	virtual HRESULT OnInit();
	virtual HRESULT OnSizeChanged();
	virtual void OnUpdate();
	virtual void OnRender();
	virtual void OnDestroy();
	virtual bool OnEvent( MSG msg );

private:
	static const UINT FrameCount = 5;

	struct Vertex
	{
		XMFLOAT3 position;
	};

	CBUFFER_ALIGN struct ConstantBuffer
	{
		XMMATRIX wvp;
		XMFLOAT4 viewPos;
		XMINT4 colVal[6];
		XMINT4 bgCol;
	};

	// Pipeline objects.
	D3D12_VIEWPORT m_viewport;
	D3D12_RECT m_scissorRect;
	ComPtr<IDXGISwapChain3> m_swapChain;
	ComPtr<ID3D12Device> m_device;
	ComPtr<ID3D12Resource> m_renderTargets[FrameCount];
	ComPtr<ID3D12CommandAllocator> m_graphicCmdAllocator;
	ComPtr<ID3D12GraphicsCommandList> m_graphicCmdList;
	ComPtr<ID3D12CommandQueue> m_graphicCmdQueue;
	ComPtr<ID3D12RootSignature> m_graphicsRootSignature;
	ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
	ComPtr<ID3D12DescriptorHeap> m_dsvHeap;
	ComPtr<ID3D12DescriptorHeap> m_cbvsrvuavHeap;
	ComPtr<ID3D12PipelineState> m_pipelineState;
	UINT m_rtvDescriptorSize;
	UINT m_cbvsrvuavDescriptorSize;

	// Compute objects.
	ComPtr<ID3D12RootSignature> m_computeRootSignature;
	ComPtr<ID3D12CommandAllocator> m_computeCmdAllocator;
	ComPtr<ID3D12CommandQueue> m_computeCmdQueue;
	ComPtr<ID3D12GraphicsCommandList> m_computeCmdList;
	ComPtr<ID3D12PipelineState> m_computeState;

	// App resources.
	ComPtr<ID3D12Resource> m_depthBuffer;
	ComPtr<ID3D12Resource> m_constantBuffer;
	ComPtr<ID3D12Resource> m_vertexBuffer;
	D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView;
	ComPtr<ID3D12Resource> m_indexBuffer;
	D3D12_INDEX_BUFFER_VIEW m_indexBufferView;
	ComPtr<ID3D12Resource> m_volumeBuffer;

	CModelViewerCamera m_camera;
	StepTimer m_timer;
	ConstantBuffer m_constantBufferData;
	UINT8* m_pCbvDataBegin;

	// Synchronization objects.
	UINT m_frameIndex;
	HANDLE m_fenceEvent;
	ComPtr<ID3D12Fence> m_fence;
	UINT64 m_fenceValue;

	UINT m_volumeWidth;
	UINT m_volumeHeight;
	UINT m_volumeDepth;

	// Indices in the root parameter table.
	enum RootParameters : UINT32
	{
		RootParameterCBV = 0,
		RootParameterSRV,
		RootParameterUAV,
		RootParametersCount
	};

	HRESULT LoadPipeline();
	HRESULT LoadAssets();
	HRESULT LoadSizeDependentResource();
	void PopulateGraphicsCommandList();
	void PopulateComputeCommandList();
	void WaitForGraphicsCmd();
	void WaitForComputeCmd();
};

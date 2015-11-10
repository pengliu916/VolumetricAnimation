
#include "stdafx.h"
#include "VolumetricAnimation.h"

VolumetricAnimation::VolumetricAnimation( UINT width, UINT height, std::wstring name ) :
	DX12Framework( width, height, name ), m_frameIndex( 0 ), m_viewport(), m_scissorRect(), m_rtvDescriptorSize( 0 )
{
	m_volumeWidth = 256;
	m_volumeHeight = 256;
	m_volumeDepth = 256;

	ZeroMemory( &m_constantBufferData, sizeof( m_constantBufferData ) );

	m_constantBufferData.colVal[0] = XMINT4( 1, 0, 0, 0 );
	m_constantBufferData.colVal[1] = XMINT4( 0, 1, 0, 1 );
	m_constantBufferData.colVal[2] = XMINT4( 0, 0, 1, 2 );
	m_constantBufferData.colVal[3] = XMINT4( 1, 1, 0, 3 );
	m_constantBufferData.colVal[4] = XMINT4( 1, 0, 1, 4 );
	m_constantBufferData.colVal[5] = XMINT4( 0, 1, 1, 5 );
	m_constantBufferData.bgCol = XMINT4( 64, 64, 64, 64 );
}

HRESULT VolumetricAnimation::OnInit()
{
	HRESULT hr;
	VRET( LoadPipeline() );
	VRET( LoadAssets() );
	VRET( LoadSizeDependentResource() );
	return S_OK;
}

// Load the rendering pipeline dependencies.
HRESULT VolumetricAnimation::LoadPipeline()
{
	HRESULT hr;

	// [TODO]: Move to project independent framework
#ifdef _DEBUG
	// Enable the D3D12 debug layer.
	ComPtr<ID3D12Debug> debugController;
	if ( SUCCEEDED( D3D12GetDebugInterface( IID_PPV_ARGS( &debugController ) ) ) )
	{
		debugController->EnableDebugLayer();
	}else
		PRINTWARN(L"Unable to enable D3D12 debug validation layer.")
#endif

	ComPtr<IDXGIFactory4> factory;
	VRET( CreateDXGIFactory1( IID_PPV_ARGS( &factory ) ) );

	if ( m_useWarpDevice )
	{
		ComPtr<IDXGIAdapter> warpAdapter;
		VRET( factory->EnumWarpAdapter( IID_PPV_ARGS( &warpAdapter ) ) );
		VRET( D3D12CreateDevice( warpAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS( &m_device ) ) );
	}
	else
	{
		ComPtr<IDXGIAdapter1> hardwareAdapter;
		GetHardwareAdapter( factory.Get(), &hardwareAdapter );
		VRET( D3D12CreateDevice( hardwareAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS( &m_device ) ) );
	}

	// [TODO]: Move to project independent framework
	// Check Direct3D 12 feature hardware support (more usage refer Direct3D 12 sdk Capability Querying)
	D3D12_FEATURE_DATA_D3D12_OPTIONS options;
	m_device->CheckFeatureSupport( D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof( options ) );
	switch ( options.ResourceBindingTier )
	{
	case D3D12_RESOURCE_BINDING_TIER_1:
		PRINTWARN( L"Tier 1 is supported." );
		break;
	case D3D12_RESOURCE_BINDING_TIER_2:
		PRINTWARN( L"Tier 1 and 2 are supported." );
		break;
	case D3D12_RESOURCE_BINDING_TIER_3:
		PRINTWARN( L"Tier 1, 2 and 3 are supported." );
		break;
	default:
		break;
	}

	// Describe and create the graphics command queue.
	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	VRET( m_device->CreateCommandQueue( &queueDesc, IID_PPV_ARGS( &m_graphicCmdQueue ) ) );
	DXDebugName( m_graphicCmdQueue );

	// Describe and create the compute command queue;
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
	VRET( m_device->CreateCommandQueue( &queueDesc, IID_PPV_ARGS( &m_computeCmdQueue ) ) );
	DXDebugName( m_computeCmdQueue );

	// Describe and create the swap chain.
	DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
	swapChainDesc.BufferCount = FrameCount;
	swapChainDesc.BufferDesc.Width = m_width;
	swapChainDesc.BufferDesc.Height = m_height;
	swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapChainDesc.OutputWindow = m_hwnd;
	swapChainDesc.SampleDesc.Count = 1;
	swapChainDesc.Windowed = TRUE;

	ComPtr<IDXGISwapChain> swapChain;
	// Swap chain needs the queue so that it can force a flush on it.
	VRET( factory->CreateSwapChain( m_graphicCmdQueue.Get(), &swapChainDesc, &swapChain ) );
	VRET( swapChain.As( &m_swapChain ) );
	DXDebugName( m_swapChain );

	// This sample does not support fullscreen transitions.
	VRET( factory->MakeWindowAssociation( m_hwnd, DXGI_MWA_NO_ALT_ENTER ) );

	m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

	// Create descriptor heaps.
	{
		// Describe and create a render target view (RTV) descriptor heap.
		D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
		rtvHeapDesc.NumDescriptors = FrameCount;
		rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		VRET( m_device->CreateDescriptorHeap( &rtvHeapDesc, IID_PPV_ARGS( &m_rtvHeap ) ) );
		DXDebugName( m_rtvHeap );

		m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_RTV );

		// Describe and create a shader resource view (SRV) and constant buffer view (CBV) descriptor heap.
		// Flags indicate that this descriptor heap can be bound to the pipeline
		// and that descriptors contained in it can be reference by a root table
		D3D12_DESCRIPTOR_HEAP_DESC cbvsrvuavHeapDesc = {};
		cbvsrvuavHeapDesc.NumDescriptors = 3; // One for SRV two for CBV (gfx and compute)
		cbvsrvuavHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		cbvsrvuavHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		VRET( m_device->CreateDescriptorHeap( &cbvsrvuavHeapDesc, IID_PPV_ARGS( &m_cbvsrvuavHeap ) ) );
		DXDebugName( m_cbvsrvuavHeap );
		
		m_cbvsrvuavDescriptorSize = m_device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );

		// Describe and create a depth stencil view (DSV) descriptor heap.
		D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
		dsvHeapDesc.NumDescriptors = 1;
		dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
		dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		VRET( m_device->CreateDescriptorHeap( &dsvHeapDesc, IID_PPV_ARGS( &m_dsvHeap ) ) );
		DXDebugName( m_dsvHeap );
	}

	VRET( m_device->CreateCommandAllocator( D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS( &m_graphicCmdAllocator ) ) );
	DXDebugName( m_graphicCmdAllocator );
	
	VRET( m_device->CreateCommandAllocator( D3D12_COMMAND_LIST_TYPE_COMPUTE, IID_PPV_ARGS( &m_computeCmdAllocator ) ) );
	DXDebugName( m_computeCmdAllocator );

	return S_OK;
}

// Load the assets.
HRESULT VolumetricAnimation::LoadAssets()
{
	HRESULT	hr;

	// Create a root signature consisting of a descriptor table with a CBV SRV and a sampler.
	{
		CD3DX12_DESCRIPTOR_RANGE ranges[3];
		CD3DX12_ROOT_PARAMETER rootParameters[3];

		ranges[0].Init( D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0 );
		ranges[1].Init( D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0 );
		ranges[2].Init( D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0 );
		rootParameters[RootParameterCBV].InitAsDescriptorTable( 1, &ranges[0], D3D12_SHADER_VISIBILITY_ALL );
		rootParameters[RootParameterSRV].InitAsDescriptorTable( 1, &ranges[1], D3D12_SHADER_VISIBILITY_PIXEL );
		rootParameters[RootParameterUAV].InitAsDescriptorTable( 1, &ranges[2], D3D12_SHADER_VISIBILITY_ALL );

		D3D12_STATIC_SAMPLER_DESC sampler = {};
		sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
		sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
		sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
		sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
		sampler.MipLODBias = 0;
		sampler.MaxAnisotropy = 0;
		sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
		sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
		sampler.MinLOD = 0.0f;
		sampler.MaxLOD = D3D12_FLOAT32_MAX;
		sampler.ShaderRegister = 0;
		sampler.RegisterSpace = 0;
		sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

		// Allow input layout and deny unnecessary access to certain pipeline stages.
		D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags =
			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;
	
		CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc;
		rootSignatureDesc.Init( _countof(rootParameters), rootParameters, 1, &sampler, rootSignatureFlags );

		ComPtr<ID3DBlob> signature;
		ComPtr<ID3DBlob> error;
		V( D3D12SerializeRootSignature( &rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error ) );
		if ( error ) PRINTERROR( reinterpret_cast< const char* >( error->GetBufferPointer() ) );

		VRET( m_device->CreateRootSignature( 0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS( &m_graphicsRootSignature ) ) );
		DXDebugName( m_graphicsRootSignature );

		// Create compute signature. Must change visibility for the SRV.
		rootParameters[RootParameterSRV].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

		CD3DX12_ROOT_SIGNATURE_DESC computeRootSignatureDesc( _countof( rootParameters ), rootParameters, 0, nullptr );
		VRET( D3D12SerializeRootSignature( &computeRootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error ) );

		VRET( m_device->CreateRootSignature( 0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS( &m_computeRootSignature ) ) );
	}

	// Create the pipeline state, which includes compiling and loading shaders.
	{
		ComPtr<ID3DBlob> vertexShader;
		ComPtr<ID3DBlob> pixelShader;
		ComPtr<ID3DBlob> computeShader;

		UINT compileFlags = 0;

		VRET( CompileShaderFromFile( GetAssetFullPath( _T( "VolumetricAnimation_shader.hlsl" ) ).c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "vsmain", "vs_5_0", compileFlags, 0, &vertexShader ) );
		VRET( CompileShaderFromFile( GetAssetFullPath( _T( "VolumetricAnimation_shader.hlsl" ) ).c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "psmain", "ps_5_0", compileFlags, 0, &pixelShader ) );
		VRET( CompileShaderFromFile( GetAssetFullPath( _T( "VolumetricAnimation_shader.hlsl" ) ).c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "csmain", "cs_5_0", compileFlags, 0, &computeShader ) );
		// Define the vertex input layout.
		D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
		{
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
		};

		CD3DX12_DEPTH_STENCIL_DESC depthStencilDesc( D3D12_DEFAULT );
		depthStencilDesc.DepthEnable = true;
		depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
		depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
		depthStencilDesc.StencilEnable = FALSE;

		// Describe and create the graphics pipeline state object (PSO).
		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
		psoDesc.InputLayout = { inputElementDescs, _countof( inputElementDescs ) };
		psoDesc.pRootSignature = m_graphicsRootSignature.Get();
		psoDesc.VS = { reinterpret_cast< UINT8* >( vertexShader->GetBufferPointer() ), vertexShader->GetBufferSize() };
		psoDesc.PS = { reinterpret_cast< UINT8* >( pixelShader->GetBufferPointer() ), pixelShader->GetBufferSize() };
		psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC( D3D12_DEFAULT );
		psoDesc.BlendState = CD3DX12_BLEND_DESC( D3D12_DEFAULT );
		psoDesc.DepthStencilState = depthStencilDesc;
		psoDesc.SampleMask = UINT_MAX;
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
		psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
		psoDesc.SampleDesc.Count = 1;
		VRET( m_device->CreateGraphicsPipelineState( &psoDesc, IID_PPV_ARGS( &m_pipelineState ) ) );
		DXDebugName( m_pipelineState );

		// Describe and create the compute pipeline state object (PSO).
		D3D12_COMPUTE_PIPELINE_STATE_DESC computePsoDesc = {};
		computePsoDesc.pRootSignature = m_computeRootSignature.Get();
		computePsoDesc.CS = { reinterpret_cast< UINT8* >( computeShader->GetBufferPointer() ), computeShader->GetBufferSize() };

		VRET( m_device->CreateComputePipelineState( &computePsoDesc, IID_PPV_ARGS( &m_computeState ) ) );
		DXDebugName( m_computeState );
	}

	// Create the compute command list.
	VRET( m_device->CreateCommandList( 0, D3D12_COMMAND_LIST_TYPE_COMPUTE, m_computeCmdAllocator.Get(),m_computeState.Get(), IID_PPV_ARGS( &m_computeCmdList ) ) );
	DXDebugName( m_computeCmdList );

	VRET( m_computeCmdList->Close() );

	// Create the graphics command list.
	VRET( m_device->CreateCommandList( 0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_graphicCmdAllocator.Get(), m_pipelineState.Get(), IID_PPV_ARGS( &m_graphicCmdList ) ) );
	DXDebugName( m_graphicCmdList );

	// Note: ComPtr's are CPU objects but this resource needs to stay in scope until
	// the command list that references it has finished executing on the GPU.
	// We will flush the GPU at the end of this method to ensure the resource is not
	// prematurely destroyed.
	ComPtr<ID3D12Resource> volumeBufferUploadHeap;

	// Create the volumeBuffer.
	{
		UINT volumeBufferSize = m_volumeDepth*m_volumeHeight*m_volumeWidth * 4 * sizeof( UINT8 );

		D3D12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer( volumeBufferSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS );
		D3D12_RESOURCE_DESC uploadBufferDesc = CD3DX12_RESOURCE_DESC::Buffer( volumeBufferSize );

		VRET( m_device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES( D3D12_HEAP_TYPE_DEFAULT ),D3D12_HEAP_FLAG_NONE,
												 &bufferDesc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS( &m_volumeBuffer ) ) );

		const UINT64 uploadBufferSize = GetRequiredIntermediateSize( m_volumeBuffer.Get(), 0, 1 );

		// Create the GPU upload buffer.
		VRET( m_device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES( D3D12_HEAP_TYPE_UPLOAD ),D3D12_HEAP_FLAG_NONE,
												 &uploadBufferDesc,D3D12_RESOURCE_STATE_GENERIC_READ,
												 nullptr,IID_PPV_ARGS( &volumeBufferUploadHeap ) ) );

		// Copy data to the intermediate upload heap and then schedule a copy 
		// from the upload heap to the Texture2D.
		UINT8* volumeBuffer = ( UINT8* ) malloc( volumeBufferSize );
		memset( volumeBuffer, 64, volumeBufferSize );
		//float radius = m_volumeHeight / 2.f;
		float a = m_volumeWidth / 2.f;
		float b = m_volumeHeight / 2.f;
		float c = m_volumeDepth / 2.f;
		float radius = sqrt( a*a + b*b + c*c );

		for ( UINT z = 0; z < m_volumeDepth; z++ )
			for ( UINT y = 0; y < m_volumeHeight; y++ )
				for ( UINT x = 0; x < m_volumeWidth; x++ )
				{
					float _x = x - m_volumeWidth / 2.f;
					float _y = y - m_volumeHeight / 2.f;
					float _z = z - m_volumeDepth / 2.f;
					//float currentRaidus =abs(_x)+abs(_y)+abs(_z);
					float currentRaidus = sqrt( _x*_x + _y*_y + _z*_z );
					float scale = currentRaidus *3.f / radius;
					UINT idx = 4 - (UINT)floor( scale );
					UINT interm = ( UINT ) ( 192 * scale +0.5f );
					UINT8 col = interm % 192+1;
					volumeBuffer[( x + y*m_volumeWidth + z*m_volumeHeight*m_volumeWidth ) * 4 + 0] += col * m_constantBufferData.colVal[idx].x;
					volumeBuffer[( x + y*m_volumeWidth + z*m_volumeHeight*m_volumeWidth ) * 4 + 1] += col * m_constantBufferData.colVal[idx].y;
					volumeBuffer[( x + y*m_volumeWidth + z*m_volumeHeight*m_volumeWidth ) * 4 + 2] += col * m_constantBufferData.colVal[idx].z;
					volumeBuffer[( x + y*m_volumeWidth + z*m_volumeHeight*m_volumeWidth ) * 4 + 3] = m_constantBufferData.colVal[idx].w;
				}
		D3D12_SUBRESOURCE_DATA volumeBufferData = {};
		volumeBufferData.pData = &volumeBuffer[0];
		volumeBufferData.RowPitch = volumeBufferSize;
		volumeBufferData.SlicePitch = volumeBufferData.RowPitch;

		UpdateSubresources( m_graphicCmdList.Get(), m_volumeBuffer.Get(), volumeBufferUploadHeap.Get(), 0, 0, 1, &volumeBufferData );
		m_graphicCmdList->ResourceBarrier( 1, &CD3DX12_RESOURCE_BARRIER::Transition( m_volumeBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS ) );

		// Describe and create a SRV for the volumeBuffer.
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.FirstElement = 0;
		srvDesc.Buffer.NumElements = m_volumeDepth*m_volumeHeight*m_volumeWidth;
		srvDesc.Buffer.StructureByteStride = 4 * sizeof( UINT8 );
		srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

		CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle( m_cbvsrvuavHeap->GetCPUDescriptorHandleForHeapStart(), RootParameterSRV, m_cbvsrvuavDescriptorSize );
		m_device->CreateShaderResourceView( m_volumeBuffer.Get(), &srvDesc, srvHandle );

		// Describe and create a UAV for the volumeBuffer.
		D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
		uavDesc.Format = DXGI_FORMAT_UNKNOWN;
		uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
		uavDesc.Buffer.FirstElement = 0;
		uavDesc.Buffer.NumElements = m_volumeWidth*m_volumeHeight*m_volumeDepth;
		uavDesc.Buffer.StructureByteStride = 4 * sizeof( UINT8 );
		uavDesc.Buffer.CounterOffsetInBytes = 0;
		uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

		CD3DX12_CPU_DESCRIPTOR_HANDLE uavHandle( m_cbvsrvuavHeap->GetCPUDescriptorHandleForHeapStart(), RootParameterUAV, m_cbvsrvuavDescriptorSize );
		m_device->CreateUnorderedAccessView( m_volumeBuffer.Get(), nullptr, &uavDesc, uavHandle );
		free( volumeBuffer );
	}

	// Create the vertex buffer.

	// Note: ComPtr's are CPU objects but this resource needs to stay in scope until
	// the command list that references it has finished executing on the GPU.
	// We will flush the GPU at the end of this method to ensure the resource is not
	// prematurely destroyed.
	ComPtr<ID3D12Resource> vertexBufferUpload;
	{
		// Define the geometry for a triangle.
		Vertex cubeVertices[] =
		{
			{ XMFLOAT3( -128.f, -128.f, -128.f ) },
			{ XMFLOAT3( -128.f, -128.f,  128.f ) },
			{ XMFLOAT3( -128.f,  128.f, -128.f ) },
			{ XMFLOAT3( -128.f,  128.f,  128.f ) },
			{ XMFLOAT3( 128.f, -128.f, -128.f )},
			{ XMFLOAT3( 128.f, -128.f,  128.f )},
			{ XMFLOAT3( 128.f,  128.f, -128.f )},
			{ XMFLOAT3( 128.f,  128.f,  128.f )},
		};

		const UINT vertexBufferSize = sizeof( cubeVertices );

		VRET( m_device->CreateCommittedResource( &CD3DX12_HEAP_PROPERTIES( D3D12_HEAP_TYPE_UPLOAD ), D3D12_HEAP_FLAG_NONE,
												 &CD3DX12_RESOURCE_DESC::Buffer( vertexBufferSize ), D3D12_RESOURCE_STATE_GENERIC_READ,
												 nullptr, IID_PPV_ARGS( &vertexBufferUpload ) ) );
		VRET( m_device->CreateCommittedResource( &CD3DX12_HEAP_PROPERTIES( D3D12_HEAP_TYPE_DEFAULT ), D3D12_HEAP_FLAG_NONE,
												 &CD3DX12_RESOURCE_DESC::Buffer( vertexBufferSize ), D3D12_RESOURCE_STATE_COPY_DEST,
												 nullptr, IID_PPV_ARGS( &m_vertexBuffer ) ) );
		DXDebugName( m_vertexBuffer );
		
		D3D12_SUBRESOURCE_DATA vertexData = {};
		vertexData.pData = reinterpret_cast< UINT8* >( cubeVertices );
		vertexData.RowPitch = vertexBufferSize;
		vertexData.SlicePitch = vertexBufferSize;

		UpdateSubresources<1>( m_graphicCmdList.Get(), m_vertexBuffer.Get(), vertexBufferUpload.Get(), 0, 0, 1, &vertexData );
		m_graphicCmdList->ResourceBarrier( 1, &CD3DX12_RESOURCE_BARRIER::Transition( m_vertexBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, 
																				  D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER ));

		// Initialize the vertex buffer view.
		m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
		m_vertexBufferView.StrideInBytes = sizeof( Vertex );
		m_vertexBufferView.SizeInBytes = vertexBufferSize;
	}

	// Create the index buffer

	// Note: ComPtr's are CPU objects but this resource needs to stay in scope until
	// the command list that references it has finished executing on the GPU.
	// We will flush the GPU at the end of this method to ensure the resource is not
	// prematurely destroyed.
	ComPtr<ID3D12Resource> indexBufferUpload;
	{
		uint16_t cubeIndices[] =
		{
			0,2,1, 1,2,3,  4,5,6, 5,7,6,  0,1,5, 0,5,4,  2,6,7, 2,7,3,  0,4,6, 0,6,2,  1,3,7, 1,7,5,
		};

		const UINT indexBufferSize = sizeof( cubeIndices );

		VRET( m_device->CreateCommittedResource( &CD3DX12_HEAP_PROPERTIES( D3D12_HEAP_TYPE_UPLOAD ), D3D12_HEAP_FLAG_NONE,
												 &CD3DX12_RESOURCE_DESC::Buffer( indexBufferSize ), D3D12_RESOURCE_STATE_GENERIC_READ,
												 nullptr, IID_PPV_ARGS( &indexBufferUpload ) ) );
		VRET( m_device->CreateCommittedResource( &CD3DX12_HEAP_PROPERTIES( D3D12_HEAP_TYPE_DEFAULT ), D3D12_HEAP_FLAG_NONE,
												 &CD3DX12_RESOURCE_DESC::Buffer( indexBufferSize ), D3D12_RESOURCE_STATE_COPY_DEST,
												 nullptr, IID_PPV_ARGS( &m_indexBuffer ) ) );
		DXDebugName( m_indexBuffer );

		D3D12_SUBRESOURCE_DATA indexData = {};
		indexData.pData = reinterpret_cast< UINT8* >( cubeIndices );
		indexData.RowPitch = indexBufferSize;
		indexData.SlicePitch = indexBufferSize;

		UpdateSubresources<1>( m_graphicCmdList.Get(), m_indexBuffer.Get(), indexBufferUpload.Get(), 0, 0, 1, &indexData );
		m_graphicCmdList->ResourceBarrier( 1, &CD3DX12_RESOURCE_BARRIER::Transition( m_indexBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, 
																				  D3D12_RESOURCE_STATE_INDEX_BUFFER ) );

		m_indexBufferView.BufferLocation = m_indexBuffer->GetGPUVirtualAddress();
		m_indexBufferView.SizeInBytes = sizeof( cubeIndices );
		m_indexBufferView.Format = DXGI_FORMAT_R16_UINT;
	}

	// Create the constant buffer
	{
		VRET( m_device->CreateCommittedResource( &CD3DX12_HEAP_PROPERTIES( D3D12_HEAP_TYPE_UPLOAD ), D3D12_HEAP_FLAG_NONE,
												 &CD3DX12_RESOURCE_DESC::Buffer( 1024 * 64 ), D3D12_RESOURCE_STATE_GENERIC_READ,
												 nullptr, IID_PPV_ARGS( &m_constantBuffer ) ) );
		DXDebugName( m_constantBuffer );

		// Describe and create a constant buffer view.
		D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
		cbvDesc.BufferLocation = m_constantBuffer->GetGPUVirtualAddress();
		cbvDesc.SizeInBytes = ( sizeof( ConstantBuffer ) + 255 ) & ~255;	// CB size is required to be 256-byte aligned.
		m_device->CreateConstantBufferView( &cbvDesc, m_cbvsrvuavHeap->GetCPUDescriptorHandleForHeapStart() );

		// Initialize and map the constant buffers. We don't unmap this until the
		// app closes. Keeping things mapped for the lifetime of the resource is okay.
		CD3DX12_RANGE readRange( 0, 0 );		// We do not intend to read from this resource on the CPU.
		VRET( m_constantBuffer->Map( 0, &readRange, reinterpret_cast< void** >( &m_pCbvDataBegin ) ) );
		memcpy( m_pCbvDataBegin, &m_constantBufferData, sizeof( m_constantBufferData ) );
	}

	// Close the command list and execute it to begin the initial GPU setup.
	VRET( m_graphicCmdList->Close() );
	ID3D12CommandList* ppCommandLists[] = { m_graphicCmdList.Get() };
	m_graphicCmdQueue->ExecuteCommandLists( _countof( ppCommandLists ), ppCommandLists );

	// Create synchronization objects and wait until assets have been uploaded to the GPU.
	{
		VRET( m_device->CreateFence( 0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS( &m_fence ) ) );
		DXDebugName( m_fence );
		m_fenceValue = 1;

		// Create an event handle to use for frame synchronization.
		m_fenceEvent = CreateEvent( nullptr, FALSE, FALSE, nullptr );
		if ( m_fenceEvent == nullptr )
		{
			VRET( HRESULT_FROM_WIN32( GetLastError() ) );
		}

		// Wait for the command list to execute; we are reusing the same command 
		// list in our main loop but for now, we just want to wait for setup to 
		// complete before continuing.
		WaitForGraphicsCmd();
	}


	XMVECTORF32 vecEye = { 500.0f, 500.0f, -500.0f };
	XMVECTORF32 vecAt = { 0.0f, 0.0f, 0.0f };
	m_camera.SetViewParams( vecEye, vecAt );
	m_camera.SetEnablePositionMovement( true );
	m_camera.SetButtonMasks( MOUSE_RIGHT_BUTTON, MOUSE_WHEEL, MOUSE_LEFT_BUTTON );

	return S_OK;
}

// Load size dependent resource
HRESULT VolumetricAnimation::LoadSizeDependentResource()
{
	HRESULT hr;
	// Create render target views (RTVs).
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle( m_rtvHeap->GetCPUDescriptorHandleForHeapStart() );
	for ( UINT i = 0; i < FrameCount; i++ )
	{
		VRET( m_swapChain->GetBuffer( i, IID_PPV_ARGS( &m_renderTargets[i] ) ) );
		DXDebugName( m_renderTargets[i] );
		m_device->CreateRenderTargetView( m_renderTargets[i].Get(), nullptr, rtvHandle );
		rtvHandle.Offset( 1, m_rtvDescriptorSize );
	}

	// Create the depth stencil.
	{
		CD3DX12_RESOURCE_DESC shadowTextureDesc( D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, static_cast< UINT >( m_width ), static_cast< UINT >( m_height ),
												 1, 1, DXGI_FORMAT_D32_FLOAT, 1, 0, D3D12_TEXTURE_LAYOUT_UNKNOWN,
												 D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL | D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE );

		D3D12_CLEAR_VALUE clearValue;	// Performance tip: Tell the runtime at resource creation the desired clear value.
		clearValue.Format = DXGI_FORMAT_D32_FLOAT;
		clearValue.DepthStencil.Depth = 1.0f;
		clearValue.DepthStencil.Stencil = 0;

		VRET( m_device->CreateCommittedResource( &CD3DX12_HEAP_PROPERTIES( D3D12_HEAP_TYPE_DEFAULT ), D3D12_HEAP_FLAG_NONE, &shadowTextureDesc,
												 D3D12_RESOURCE_STATE_DEPTH_WRITE, &clearValue, IID_PPV_ARGS( &m_depthBuffer ) ) );
		DXDebugName( m_depthBuffer );

		// Create the depth stencil view.
		m_device->CreateDepthStencilView( m_depthBuffer.Get(), nullptr, m_dsvHeap->GetCPUDescriptorHandleForHeapStart() );
	}

	m_viewport.Width = static_cast< float >( m_width );
	m_viewport.Height = static_cast< float >( m_height );
	m_viewport.MaxDepth = 1.0f;

	m_scissorRect.right = static_cast< LONG >( m_width );
	m_scissorRect.bottom = static_cast< LONG >( m_height );

	float fAspectRatio = m_width / ( FLOAT ) m_height;
	m_camera.SetProjParams( XM_PI / 4, fAspectRatio, 0.01f, 1250.0f );
	m_camera.SetWindow( m_width, m_height );
	return S_OK;
}

// Update frame-based values.
void VolumetricAnimation::OnUpdate()
{
	m_timer.Tick( NULL );
	float frameTime = static_cast< float >( m_timer.GetElapsedSeconds() );
	float frameChange = 2.0f * frameTime;

	m_camera.FrameMove( frameTime );
}

// Render the scene.
void VolumetricAnimation::OnRender()
{
	HRESULT hr;
	PopulateComputeCommandList();
	ID3D12CommandList* ppComputeCommandLists[] = { m_computeCmdList.Get() };
	m_computeCmdQueue->ExecuteCommandLists( _countof( ppComputeCommandLists ), ppComputeCommandLists );

	WaitForComputeCmd();

	// Record all the commands we need to render the scene into the command list.
	PopulateGraphicsCommandList();

	// Execute the command list.
	ID3D12CommandList* ppGraphicsCommandLists[] = { m_graphicCmdList.Get() };
	m_graphicCmdQueue->ExecuteCommandLists( _countof( ppGraphicsCommandLists ), ppGraphicsCommandLists );

	// Present the frame.
	V( m_swapChain->Present( 0, 0 ) );
	m_frameIndex = (m_frameIndex+1)% FrameCount;
	//m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

	WaitForGraphicsCmd();


}

HRESULT VolumetricAnimation::OnSizeChanged()
{
	HRESULT hr;
	// Flush all current GPU commands.
	WaitForGraphicsCmd();

	// Release the resources holding references to the swap chain (requirement of
	// IDXGISwapChain::ResizeBuffers) and reset the frame fence values to the
	// current fence value.
	for ( UINT n = 0; n < FrameCount; n++ )
	{
		m_renderTargets[n].Reset();
	}

	// Resize the swap chain to the desired dimensions.
	DXGI_SWAP_CHAIN_DESC desc = {};
	m_swapChain->GetDesc( &desc );
	VRET( m_swapChain->ResizeBuffers( FrameCount, m_width, m_height, desc.BufferDesc.Format, desc.Flags ) );

	m_depthBuffer.Reset();

	VRET( LoadSizeDependentResource() );

	// Reset the frame index to the current back buffer index.
	m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
	return S_OK;
}


void VolumetricAnimation::OnDestroy()
{
	// Wait for the GPU to be done with all resources.
	WaitForGraphicsCmd();

	CloseHandle( m_fenceEvent );
}

bool VolumetricAnimation::OnEvent( MSG msg )
{
	m_camera.HandleMessages( msg.hwnd, msg.message, msg.wParam, msg.lParam );
	return false;
}

void VolumetricAnimation::PopulateGraphicsCommandList()
{
	HRESULT hr;
	// Command list allocators can only be reset when the associated 
	// command lists have finished execution on the GPU; apps should use 
	// fences to determine GPU execution progress.
	V( m_graphicCmdAllocator->Reset() );

	// However, when ExecuteCommandList() is called on a particular command 
	// list, that command list can then be reset at any time and must be before 
	// re-recording.
	V( m_graphicCmdList->Reset( m_graphicCmdAllocator.Get(), m_pipelineState.Get() ) );

	XMMATRIX view = m_camera.GetViewMatrix();
	XMMATRIX proj = m_camera.GetProjMatrix();

	XMMATRIX world = XMMatrixRotationY( static_cast< float >( m_timer.GetTotalSeconds() ) );
	m_constantBufferData.wvp = XMMatrixMultiply( view, proj );
	//m_constantBufferData.wvp = XMMatrixMultiply( XMMatrixMultiply( world, view ), proj );
	XMStoreFloat4( &m_constantBufferData.viewPos, m_camera.GetEyePt() );
	
	memcpy( m_pCbvDataBegin, &m_constantBufferData, sizeof( m_constantBufferData ) );

	// Set necessary state.
	m_graphicCmdList->SetGraphicsRootSignature( m_graphicsRootSignature.Get() );

	ID3D12DescriptorHeap* ppHeaps[] = { m_cbvsrvuavHeap.Get() };
	m_graphicCmdList->SetDescriptorHeaps( _countof( ppHeaps ), ppHeaps );

	CD3DX12_GPU_DESCRIPTOR_HANDLE cbvHandle( m_cbvsrvuavHeap->GetGPUDescriptorHandleForHeapStart(), RootParameterCBV, m_cbvsrvuavDescriptorSize );
	CD3DX12_GPU_DESCRIPTOR_HANDLE srvHandle( m_cbvsrvuavHeap->GetGPUDescriptorHandleForHeapStart(), RootParameterSRV, m_cbvsrvuavDescriptorSize );

	m_graphicCmdList->SetGraphicsRootDescriptorTable( RootParameterCBV, cbvHandle );
	m_graphicCmdList->SetGraphicsRootDescriptorTable( RootParameterSRV, srvHandle );

	m_graphicCmdList->RSSetViewports( 1, &m_viewport );
	m_graphicCmdList->RSSetScissorRects( 1, &m_scissorRect );

	// Indicate that the back buffer will be used as a render target.
	D3D12_RESOURCE_BARRIER resourceBarriersBefore[] = {
		CD3DX12_RESOURCE_BARRIER::Transition( m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET ),
		CD3DX12_RESOURCE_BARRIER::Transition( m_volumeBuffer.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE )
	};
	m_graphicCmdList->ResourceBarrier( 2, resourceBarriersBefore );

	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle( m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex, m_rtvDescriptorSize );
	m_graphicCmdList->OMSetRenderTargets( 1, &rtvHandle, FALSE, &m_dsvHeap->GetCPUDescriptorHandleForHeapStart() );

	// Record commands.
	const float clearColor[] = { 0.0f, 0.0f, 0.0f, 0.0f };
	m_graphicCmdList->ClearRenderTargetView( rtvHandle, clearColor, 0, nullptr );
	m_graphicCmdList->ClearDepthStencilView( m_dsvHeap->GetCPUDescriptorHandleForHeapStart(), D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr );
	m_graphicCmdList->IASetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
	m_graphicCmdList->IASetVertexBuffers( 0, 1, &m_vertexBufferView );
	m_graphicCmdList->IASetIndexBuffer( &m_indexBufferView );
	m_graphicCmdList->DrawIndexedInstanced( 36, 1, 0, 0, 0 );

	// Indicate that the back buffer will now be used to present.
	D3D12_RESOURCE_BARRIER resourceBarriersAfter[] = {
		CD3DX12_RESOURCE_BARRIER::Transition( m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT ),
		CD3DX12_RESOURCE_BARRIER::Transition( m_volumeBuffer.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS )
	};
	m_graphicCmdList->ResourceBarrier( 2, resourceBarriersAfter );
	V( m_graphicCmdList->Close() );
}

void VolumetricAnimation::PopulateComputeCommandList()
{
	HRESULT hr;
	V( m_computeCmdAllocator->Reset() );
	V( m_computeCmdList->Reset( m_computeCmdAllocator.Get(), m_computeState.Get() ) );
	m_computeCmdList->SetPipelineState( m_computeState.Get() );
	m_computeCmdList->SetComputeRootSignature( m_computeRootSignature.Get() );
	ID3D12DescriptorHeap* ppHeaps[] = { m_cbvsrvuavHeap.Get() };
	m_computeCmdList->SetDescriptorHeaps( _countof( ppHeaps ), ppHeaps );
	CD3DX12_GPU_DESCRIPTOR_HANDLE cbvHandle( m_cbvsrvuavHeap->GetGPUDescriptorHandleForHeapStart(), RootParameterCBV, m_cbvsrvuavDescriptorSize );
	CD3DX12_GPU_DESCRIPTOR_HANDLE uavHandle( m_cbvsrvuavHeap->GetGPUDescriptorHandleForHeapStart(), RootParameterUAV, m_cbvsrvuavDescriptorSize );

	m_computeCmdList->SetComputeRootDescriptorTable( RootParameterCBV, cbvHandle );
	m_computeCmdList->SetComputeRootDescriptorTable( RootParameterUAV, uavHandle );
	m_computeCmdList->Dispatch( m_volumeWidth / 8, m_volumeHeight/ 8, m_volumeDepth/ 8);
	m_computeCmdList->Close();
}

void VolumetricAnimation::WaitForGraphicsCmd()
{
	HRESULT hr;
	// WAITING FOR THE FRAME TO COMPLETE BEFORE CONTINUING IS NOT BEST PRACTICE.
	// This is code implemented as such for simplicity. More advanced samples 
	// illustrate how to use fences for efficient resource usage.

	// Signal and increment the fence value.
	const UINT64 fence = m_fenceValue;
	V( m_graphicCmdQueue->Signal( m_fence.Get(), fence ) );
	m_fenceValue++;

	// Wait until the previous frame is finished.
	if ( m_fence->GetCompletedValue() < fence )
	{
		V( m_fence->SetEventOnCompletion( fence, m_fenceEvent ) );
		WaitForSingleObject( m_fenceEvent, INFINITE );
	}
}

void VolumetricAnimation::WaitForComputeCmd()
{
	HRESULT hr;

	// Signal and increment the fence value.
	const UINT64 fence = m_fenceValue;
	V( m_computeCmdQueue->Signal( m_fence.Get(), fence ) );
	m_fenceValue++;

	// Wait until the previous frame is finished.
	if ( m_fence->GetCompletedValue() < fence )
	{
		V( m_fence->SetEventOnCompletion( fence, m_fenceEvent ) );
		WaitForSingleObject( m_fenceEvent, INFINITE );
	}
}
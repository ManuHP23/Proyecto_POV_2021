#include "stdafx.h"

struct Vertex {
	Vertex(float x, float y, float z, float u, float v) : pos(x, y, z), texCoord(u, v) {}
	XMFLOAT3 pos;
	XMFLOAT2 texCoord;
};

int WINAPI WinMain(HINSTANCE hInstance,    //Main windows function
	HINSTANCE hPrevInstance,
	LPSTR lpCmdLine,
	int nShowCmd)

{
	// crear la ventana
	if (!InitializeWindow(hInstance, nShowCmd, FullScreen))
	{
		MessageBox(0, L"Window Initialization - Failed",
			L"Error", MB_OK);
		return 1;
	}

	// inicializar direct3d
	if (!InitD3D())
	{
		MessageBox(0, L"Failed to initialize direct3d 12",
			L"Error", MB_OK);
		Cleanup();
		return 1;
	}

	// empezar el main loop
	mainloop();

	// queremos esperar a que la gpu ejecuta la command list antes de empezar a liberar
	WaitForPreviousFrame();

	// cerrar fence event
	CloseHandle(fenceEvent);

	// liberar todo
	Cleanup();

	return 0;
}

// crear y mostrar la ventana
bool InitializeWindow(HINSTANCE hInstance,
	int ShowWnd,
	bool fullscreen)

{
	if (fullscreen)
	{
		HMONITOR hmon = MonitorFromWindow(hwnd,
			MONITOR_DEFAULTTONEAREST);
		MONITORINFO mi = { sizeof(mi) };
		GetMonitorInfo(hmon, &mi);

		Width = mi.rcMonitor.right - mi.rcMonitor.left;
		Height = mi.rcMonitor.bottom - mi.rcMonitor.top;
	}

	WNDCLASSEX wc;

	wc.cbSize = sizeof(WNDCLASSEX);
	wc.style = CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc = WndProc;
	wc.cbClsExtra = NULL;
	wc.cbWndExtra = NULL;
	wc.hInstance = hInstance;
	wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 2);
	wc.lpszMenuName = NULL;
	wc.lpszClassName = WindowName;
	wc.hIconSm = LoadIcon(NULL, IDI_APPLICATION);

	if (!RegisterClassEx(&wc))
	{
		MessageBox(NULL, L"Error registering class",
			L"Error", MB_OK | MB_ICONERROR);
		return false;
	}

	hwnd = CreateWindowEx(NULL,
		WindowName,
		WindowTitle,
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT,
		Width, Height,
		NULL,
		NULL,
		hInstance,
		NULL);

	if (!hwnd)
	{
		MessageBox(NULL, L"Error creating window",
			L"Error", MB_OK | MB_ICONERROR);
		return false;
	}

	if (fullscreen)
	{
		SetWindowLong(hwnd, GWL_STYLE, 0);
	}

	ShowWindow(hwnd, ShowWnd);
	UpdateWindow(hwnd);

	return true;
}

void mainloop() {
	MSG msg;
	ZeroMemory(&msg, sizeof(MSG));

	while (Running)
	{
		if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
		{
			if (msg.message == WM_QUIT)
				break;

			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		else {
			// ejecutar codigo de juego
			Update(); // actulizar la logica de juego
			Render(); // ejecutar la command queue (el renderizado de la escena es el resultado de la ejecucion de la command list por la gpu)
		}
	}
}

LRESULT CALLBACK WndProc(HWND hwnd,
	UINT msg,
	WPARAM wParam,
	LPARAM lParam)
{
	switch (msg)
	{

	case WM_KEYDOWN:
		if (wParam == VK_ESCAPE) {
			if (MessageBox(0, L"Are you sure you want to exit?",
				L"Really?", MB_YESNO | MB_ICONQUESTION) == IDYES)
			{
				Running = false;
				DestroyWindow(hwnd);
			}
		}
		return 0;

	case WM_DESTROY: // se presiona el boton x de la parte superior derecha de la esquina de la ventana
		Running = false;
		PostQuitMessage(0);
		return 0;
	}
	return DefWindowProc(hwnd,
		msg,
		wParam,
		lParam);
}

BYTE* imageData;

bool InitD3D()
{
	HRESULT hr;

	// -- Crear Device (dispositivo) -- //

	IDXGIFactory4* dxgiFactory;
	hr = CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory));
	if (FAILED(hr))
	{
		return false;
	}

	IDXGIAdapter1* adapter; // los adaptadores son la tarjeta gráfica (esto incluye los gráficos integrados en la placa base)

	int adapterIndex = 0; // comenzaremos a buscar dispositivos gráficos compatibles con directx 12 a partir del índice 0

	bool adapterFound = false; // establecemos como verdadero cuando se encuentra uno

							   // encontrar el primer hardware gpu que soporte d3d 12
	while (dxgiFactory->EnumAdapters1(adapterIndex, &adapter) != DXGI_ERROR_NOT_FOUND)
	{
		DXGI_ADAPTER_DESC1 desc;
		adapter->GetDesc1(&desc);

		if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
		{
			// no queremos un software device
			continue;
		}

		// queremos un dispositivo que sea compatible con direct3d 12 (11 o superior)
		hr = D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr);
		if (SUCCEEDED(hr))
		{
			adapterFound = true;
			break;
		}

		adapterIndex++;
	}

	if (!adapterFound)
	{
		Running = false;
		return false;
	}

	// Crear el dispositivo
	hr = D3D12CreateDevice(
		adapter,
		D3D_FEATURE_LEVEL_11_0,
		IID_PPV_ARGS(&device)
	);
	if (FAILED(hr))
	{
		Running = false;
		return false;
	}

	// -- Crear la direct command queue -- //

	D3D12_COMMAND_QUEUE_DESC cqDesc = {};
	cqDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	cqDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT; // direct significa que la gpu puede ejecutar directamente la command queue

	hr = device->CreateCommandQueue(&cqDesc, IID_PPV_ARGS(&commandQueue)); // crear la command queue
	if (FAILED(hr))
	{
		Running = false;
		return false;
	}

	// -- Crear la Swap Chain (double/tripple buffering) -- //

	DXGI_MODE_DESC backBufferDesc = {}; // esto es para describir nuestro display mode
	backBufferDesc.Width = Width; // buffer ancho
	backBufferDesc.Height = Height; // buffer alto
	backBufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // formato del buffer (rgba 32 bits, 8 bits para cada canal)

														// describe nuestro multi-sampling. No hay multi-sampling, por lo que ponemos count a 1 (necesitamos al menos un sample)
	DXGI_SAMPLE_DESC sampleDesc = {};
	sampleDesc.Count = 1; // multisample count (sin multisampling, por lo que ponemos 1, ya que todavia necesitamos 1 sample)

						  // Describe y crea swap chain.
	DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
	swapChainDesc.BufferCount = frameBufferCount; // numero de buffers que tenemos
	swapChainDesc.BufferDesc = backBufferDesc; // descripcion de nuestro back buffer 
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; // esto le dice al pipeline que renderice esta swap chain
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; // dxgi descartara el buffer (datos) despues de llamar al present
	swapChainDesc.OutputWindow = hwnd; // handle de nuestra window
	swapChainDesc.SampleDesc = sampleDesc; // descripcion multi-sampling 
	swapChainDesc.Windowed = !FullScreen; // poner true. Si está en pantalla completa llamar a SetFullScreenState con verdadero para obtener fps sin límite

	IDXGISwapChain* tempSwapChain;

	dxgiFactory->CreateSwapChain(
		commandQueue, // la cola se vaciará una vez que se cree la swapchain.
		&swapChainDesc, // le damos la descripcion de la swap chain creada antes
		&tempSwapChain // almacenar la swap chain creada en una interfaz temporal IDXGISwapChain
	);

	swapChain = static_cast<IDXGISwapChain3*>(tempSwapChain);

	frameIndex = swapChain->GetCurrentBackBufferIndex();

	// -- Crear los Back Buffers (renderizar target views) Descriptor Heap -- //

	// describir y crear un rtv descriptor heap 
	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
	rtvHeapDesc.NumDescriptors = frameBufferCount; // numero de descriptors para este heap.
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV; // este heap es un render target view heap

													   // Este heap no sera directamente referenciado por los shaders (no el shader visible), almacenará el output desde el pipeline
													   // de lo contrario estableceriamos la heap's flag a D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	hr = device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&rtvDescriptorHeap));
	if (FAILED(hr))
	{
		Running = false;
		return false;
	}

	// obtiene el tamaño de un descriptor en este heap (este es un rtv heap, por lo que solo los rtv descriptors podrian almacenarlo.
	// el tamaño del descriptor pueden variar de un dispositivo a otro, por lo que no hay un tamaño prestablecido y
	// debemos preguntar al dispositivo. Usaremos este tamaño para incrementar descriptor handle offset
	rtvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	// obtiene un handle para el primer descriptor en el descriptor heap. un handle es basicamente un puntero,
	// pero no podemos usarlo literalmente como puntero en c++.
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(rtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

	// Crea un RTV para cada buffer (double buffering si son dos buffers, tripple buffering si son 3).
	for (int i = 0; i < frameBufferCount; i++)
	{
		// primero obtiene el n buffer en la swap chain y lo almacena en la n
		// posicion de nuestro ID3D12Resource array
		hr = swapChain->GetBuffer(i, IID_PPV_ARGS(&renderTargets[i]));
		if (FAILED(hr))
		{
			Running = false;
			return false;
		}

		// "creamos" un render target view vinculado a swap chain buffer (ID3D12Resource[n]) para el rtv handle
		device->CreateRenderTargetView(renderTargets[i], nullptr, rtvHandle);

		// incrementa el rtv handle segun el tamaño del rtv descriptor anterior
		rtvHandle.Offset(1, rtvDescriptorSize);
	}

	// -- Crear los Command Allocators -- //

	for (int i = 0; i < frameBufferCount; i++)
	{
		hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator[i]));
		if (FAILED(hr))
		{
			Running = false;
			return false;
		}
	}

	// -- Crear la Command List -- //

	// crea la command list con el primer allocator
	hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator[frameIndex], NULL, IID_PPV_ARGS(&commandList));
	if (FAILED(hr))
	{
		Running = false;
		return false;
	}

	// -- Crear una Fence & Fence Event -- //

	// crear las fences
	for (int i = 0; i < frameBufferCount; i++)
	{
		hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence[i]));
		if (FAILED(hr))
		{
			Running = false;
			return false;
		}
		fenceValue[i] = 0; // establecemos el valor inicial de la fence a 0
	}

	// creamos un handle para un fence event
	fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	if (fenceEvent == nullptr)
	{
		Running = false;
		return false;
	}

	// creamos la root signature

	// creamos un root descriptor, que explica donde encontrar los datos para este root parameter
	D3D12_ROOT_DESCRIPTOR rootCBVDescriptor;
	rootCBVDescriptor.RegisterSpace = 0;
	rootCBVDescriptor.ShaderRegister = 0;

	// creamos un descriptor range (descriptor table) y lo completamos
	// este es un range de descriptors dentro de un descriptor heap
	D3D12_DESCRIPTOR_RANGE  descriptorTableRanges[1]; // solo un range 
	descriptorTableRanges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; // este es un range de shader resource views (descriptors)
	descriptorTableRanges[0].NumDescriptors = 1; // solo tenemos una textura, por lo que el range es 1
	descriptorTableRanges[0].BaseShaderRegister = 0; // start index del shader registers en el range
	descriptorTableRanges[0].RegisterSpace = 0; // space 0. normalmente es cero
	descriptorTableRanges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND; // esto agrega el range al final de la root signature descriptor tables

	// cramos la descriptor table
	D3D12_ROOT_DESCRIPTOR_TABLE descriptorTable;
	descriptorTable.NumDescriptorRanges = _countof(descriptorTableRanges); // solo tenemos un range
	descriptorTable.pDescriptorRanges = &descriptorTableRanges[0]; // el puntero al inicio de nuestro ranges array

	// craeamos un root parameter para la root descriptor y lo completamos
	D3D12_ROOT_PARAMETER  rootParameters[2]; // dos parametros
	rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV; // esta es una constant buffer view root descriptor
	rootParameters[0].Descriptor = rootCBVDescriptor; // esta es la root descriptor para este root parameter
	rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX; // nuestro pixel shader sera el unico shader que acceda a este parametro por ahora

	// completamos el parameter para nuestra descriptor table. Es mejor ordenar los parametros por frecuencia de cambio. Nuestro 
	// constant buffer sera cambiado multiples veces por frame, miestras que nuestra descriptor table no
	rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; // esta es una descriptor table
	rootParameters[1].DescriptorTable = descriptorTable; // este es nuestro descriptor table para este root parameter
	rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL; // nuestro pixel shadersera el unico shader que acceda a este parametro por ahora

	// creamos un static sampler
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

	CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc;
	rootSignatureDesc.Init(_countof(rootParameters), // tenemos dos root parameters
		rootParameters, // un puntero hacia el inicio de nuestro root parameters array
		1, // tenemos un static sampler
		&sampler, // un puntero a nuestro static sampler (array)
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT | // podemos negar etapas del sombreado para un mejor rendimiento
		D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS);

	ID3DBlob* errorBuff; // un buffer que contiene los error data por si hubieran
	ID3DBlob* signature;
	hr = D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &errorBuff);
	if (FAILED(hr))
	{
		OutputDebugStringA((char*)errorBuff->GetBufferPointer());
		return false;
	}

	hr = device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&rootSignature));
	if (FAILED(hr))
	{
		return false;
	}

	// creamos el pixel shader y vertex shader

	// cuando activamos el debug, podemos compilar los archivos del shader en tiempo de ejecucion
	// pero para publicarlo, podemos compilar los hlsl shaders
	// con fxc.exe para crear los archivos .cso, los cuales contienen el shader
	// bytecode. Podemos cargar los archivos .cso en tiempo de ejecucion para obtener
	// el shader bytecode, lo cual es mas rapido que compilarlos en tiempo de ejecucion.


	//compilar vertex shader
	ID3DBlob* vertexShader; // d3d blob que contiene el vertex shader bytecode
	hr = D3DCompileFromFile(L"VertexShader.hlsl",
		nullptr,
		nullptr,
		"main",
		"vs_5_0",
		D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION,
		0,
		&vertexShader,
		&errorBuff);
	if (FAILED(hr))
	{
		OutputDebugStringA((char*)errorBuff->GetBufferPointer());
		Running = false;
		return false;
	}

	//completar una shader bytecode structure, que es básicamente un puntero 
	//al shader bytecode y al tamaño del shader bytecode 
	D3D12_SHADER_BYTECODE vertexShaderBytecode = {};
	vertexShaderBytecode.BytecodeLength = vertexShader->GetBufferSize();
	vertexShaderBytecode.pShaderBytecode = vertexShader->GetBufferPointer();

	// compilar el pixel shader
	ID3DBlob* pixelShader;
	hr = D3DCompileFromFile(L"PixelShader.hlsl",
		nullptr,
		nullptr,
		"main",
		"ps_5_0",
		D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION,
		0,
		&pixelShader,
		&errorBuff);
	if (FAILED(hr))
	{
		OutputDebugStringA((char*)errorBuff->GetBufferPointer());
		Running = false;
		return false;
	}

	// completar el shader bytecode structure para el pixel shader
	D3D12_SHADER_BYTECODE pixelShaderBytecode = {};
	pixelShaderBytecode.BytecodeLength = pixelShader->GetBufferSize();
	pixelShaderBytecode.pShaderBytecode = pixelShader->GetBufferPointer();

	// creamos el input layout

	// El input layout es usado por el Input Assembler quien le dice a este
	// como leer los datos de vertices vinculados a el

	D3D12_INPUT_ELEMENT_DESC inputLayout[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};

	// completamos la descripcion de estructura del input layout 
	D3D12_INPUT_LAYOUT_DESC inputLayoutDesc = {};

	// podemos obtener el numero de elementos en un array mediante "sizeof(array) / sizeof(arrayElementType)"
	inputLayoutDesc.NumElements = sizeof(inputLayout) / sizeof(D3D12_INPUT_ELEMENT_DESC);
	inputLayoutDesc.pInputElementDescs = inputLayout;

	// creamos un pipeline state object (PSO)

	// Lo normal es tener muchos PSO para diferentes shaders
	// o diferentes combinaciones de shaders, diferentes blend states o diferentes rasterizer states,
	// diferentes tipos de topologia (puntos, lineas, triangulos) o diferente numero
	// de render targets que necesitaran de un PSO

	// VS es el unico shader necesario para un PSO.




	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {}; // una estructura para definir un pso
	psoDesc.InputLayout = inputLayoutDesc; // la estructura describe nuestra input layout
	psoDesc.pRootSignature = rootSignature; // la root signature que describe el input data que necesita este PSO
	psoDesc.VS = vertexShaderBytecode; // la estructura describe donde encontrar el vertex shader bytecode y como de grande es
	psoDesc.PS = pixelShaderBytecode; // lo mismo que VS but pero para el pixel shader
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE; // tipo de topologia que estamos dibujando
	psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM; // formato del render target
	psoDesc.SampleDesc = sampleDesc; // debe ser la misma sample description que la swapchain y el depth/stencil buffer 
	psoDesc.SampleMask = 0xffffffff; // la sample mask tiene que ver con el multi-sampling. 0xffffffff significa que el muestreo de puntos esta hecho
	psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT); // el default rasterizer state.
	psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT); // el default blend state.
	psoDesc.NumRenderTargets = 1; // solo estamos vinculando 1 render target
	psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT); // el default depth stencil state

	// crear el pso
	hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipelineStateObject));
	if (FAILED(hr))
	{
		Running = false;
		return false;
	}

	// Creamos el vertex buffer

	// un cuadrado
	Vertex vList[] = {
		// cara frontal
		{ -0.5f,  0.5f, -0.5f, 0.0f, 0.0f },
		{  0.5f, -0.5f, -0.5f, 1.0f, 1.0f },
		{ -0.5f, -0.5f, -0.5f, 0.0f, 1.0f },
		{  0.5f,  0.5f, -0.5f, 1.0f, 0.0f },

		// cara lateral derecha
		{  0.5f, -0.5f, -0.5f, 0.0f, 1.0f },
		{  0.5f,  0.5f,  0.5f, 1.0f, 0.0f },
		{  0.5f, -0.5f,  0.5f, 1.0f, 1.0f },
		{  0.5f,  0.5f, -0.5f, 0.0f, 0.0f },

		// cara lateral izquierda
		{ -0.5f,  0.5f,  0.5f, 0.0f, 0.0f },
		{ -0.5f, -0.5f, -0.5f, 1.0f, 1.0f },
		{ -0.5f, -0.5f,  0.5f, 0.0f, 1.0f },
		{ -0.5f,  0.5f, -0.5f, 1.0f, 0.0f },

		// cara trasera
		{  0.5f,  0.5f,  0.5f, 0.0f, 0.0f },
		{ -0.5f, -0.5f,  0.5f, 1.0f, 1.0f },
		{  0.5f, -0.5f,  0.5f, 0.0f, 1.0f },
		{ -0.5f,  0.5f,  0.5f, 1.0f, 0.0f },

		// cara superior
		{ -0.5f,  0.5f, -0.5f, 0.0f, 1.0f },
		{  0.5f,  0.5f,  0.5f, 1.0f, 0.0f },
		{  0.5f,  0.5f, -0.5f, 1.0f, 1.0f },
		{ -0.5f,  0.5f,  0.5f, 0.0f, 0.0f },

		// cara inferior
		{  0.5f, -0.5f,  0.5f, 0.0f, 0.0f },
		{ -0.5f, -0.5f, -0.5f, 1.0f, 1.0f },
		{  0.5f, -0.5f, -0.5f, 0.0f, 1.0f },
		{ -0.5f, -0.5f,  0.5f, 1.0f, 0.0f },
	};

	int vBufferSize = sizeof(vList);

	// creamos el default heap
	// el default heap es memoria en la GPU. Solo la GPU tiene acceso a esta memoria
	// Para obtener datos en este heap, tendremos que cargar los datos usando
	// un upload heap
	hr = device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), // un default heap
		D3D12_HEAP_FLAG_NONE, // sin flags
		&CD3DX12_RESOURCE_DESC::Buffer(vBufferSize), // descripcion del recurso para el buffer
		D3D12_RESOURCE_STATE_COPY_DEST, // iniciaremos este heap en el estado de destino de la copia ya que copiaremos los datos
										// desde el upload heap hasta este heap
		nullptr, // El valor optimizado debe ser nulo para este tipo de recursos usado para render targets y depth/stencil buffers
		IID_PPV_ARGS(&vertexBuffer));
	if (FAILED(hr))
	{
		Running = false;
		return false;
	}

	// Podemos poner nombre a los resource heaps para que, cuando depuramos con el depurador de gráficos, sepamos que recurso estamos buscando 
	vertexBuffer->SetName(L"Vertex Buffer Resource Heap");

	// creamos el upload heap
	// los upload heaps son usados para cargar datos a la GPU. La CPU puede escribir en ella. La GPU puede leer en ella.
	// Cargaremos el vertex buffer usando este heap para el default heap
	ID3D12Resource* vBufferUploadHeap;
	hr = device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD), // upload heap
		D3D12_HEAP_FLAG_NONE, // sin flags
		&CD3DX12_RESOURCE_DESC::Buffer(vBufferSize), // descripcion del recurso para el buffer
		D3D12_RESOURCE_STATE_GENERIC_READ, // La GPU leera desde este buffer y copiara su contenido al default heap
		nullptr,
		IID_PPV_ARGS(&vBufferUploadHeap));
	if (FAILED(hr))
	{
		Running = false;
		return false;
	}
	vBufferUploadHeap->SetName(L"Vertex Buffer Upload Resource Heap");

	// almacenamiento del vertex buffer en el upload heap
	D3D12_SUBRESOURCE_DATA vertexData = {};
	vertexData.pData = reinterpret_cast<BYTE*>(vList); // puntero a nuestro array de vertices
	vertexData.RowPitch = vBufferSize; // tamaño de todos los datos de vertices del triangulo
	vertexData.SlicePitch = vBufferSize; // tambien tañamo de los datos de vertices del triangulo

	// ahora estamos creando un comando con la command list para copiar los datos desde
	// el upload heap hasta el default heap
	UpdateSubresources(commandList, vertexBuffer, vBufferUploadHeap, 0, 0, 1, &vertexData);

	// transición de los datos del buffer de vertices desde el copy destination state hasta el vertex buffer state
	commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(vertexBuffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER));

	// Creamos el index buffer

	// un cuadrado (2 triangulos)
	DWORD iList[] = {
		// cara frontal
		0, 1, 2, // primer triangulo
		0, 3, 1, // segundo triangulo

		// lado izq
		4, 5, 6, // primer triangulo
		4, 7, 5, // segundo triangulo

		// lado dcho
		8, 9, 10, // primer triangulo
		8, 11, 9, // segundo triangulo

		// cara trasera
		12, 13, 14, // primer triangulo
		12, 15, 13, // segundo triangulo

		// lado superior
		16, 17, 18, // primer triangulo
		16, 19, 17, // segundo triangulo

		// lado inferior
		20, 21, 22, // primer triangulo
		20, 23, 21, // segundo triangulo
	};

	int iBufferSize = sizeof(iList);

	numCubeIndices = sizeof(iList) / sizeof(DWORD);

	// creamos el default heap que contiene el index buffer
	hr = device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), // el default heap
		D3D12_HEAP_FLAG_NONE, // sin flags
		&CD3DX12_RESOURCE_DESC::Buffer(iBufferSize), // descripcion de recurso para un buffer
		D3D12_RESOURCE_STATE_COPY_DEST, // empezar en el copy destination state
		nullptr, // el valor optimizado debe ser nulo para este tipo de recurso 
		IID_PPV_ARGS(&indexBuffer));
	if (FAILED(hr))
	{
		Running = false;
		return false;
	}

	// podemos darle a los resource heaps un nombre. Asi cuando utilicemos el debug con el graphics debugger sabremos el recurso que estamos analizando
	vertexBuffer->SetName(L"Index Buffer Resource Heap");

	// creamos el upload heap para cargar el index buffer
	ID3D12Resource* iBufferUploadHeap;
	hr = device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD), // upload heap
		D3D12_HEAP_FLAG_NONE, // sin flags
		&CD3DX12_RESOURCE_DESC::Buffer(vBufferSize), // descripcion de recurso para un buffer
		D3D12_RESOURCE_STATE_GENERIC_READ, // la GPU leera desde este buffer y copiara su contenido al default heap
		nullptr,
		IID_PPV_ARGS(&iBufferUploadHeap));
	if (FAILED(hr))
	{
		Running = false;
		return false;
	}
	vBufferUploadHeap->SetName(L"Index Buffer Upload Resource Heap");

	// almacenamiento del vertex buffer en el upload heap
	D3D12_SUBRESOURCE_DATA indexData = {};
	indexData.pData = reinterpret_cast<BYTE*>(iList); // puntero al index array
	indexData.RowPitch = iBufferSize; // tamaño de todo nuestro index buffer
	indexData.SlicePitch = iBufferSize; // el tamaño de nuestro index buffer

	// ahora creamos un command con la command list para copiar los datos desde el
	// upload heap al default heap
	UpdateSubresources(commandList, indexBuffer, iBufferUploadHeap, 0, 0, 1, &indexData);

	// transicion de los datos del vertex buffer desde copy destination state al vertex buffer state
	commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(indexBuffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER));

	// Creamos el depth/stencil buffer

	// creamos un depth stencil descriptor heap por lo que podemos obtener un puntero al depth stencil buffer
	D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
	dsvHeapDesc.NumDescriptors = 1;
	dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	hr = device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&dsDescriptorHeap));
	if (FAILED(hr))
	{
		Running = false;
		return false;
	}

	D3D12_DEPTH_STENCIL_VIEW_DESC depthStencilDesc = {};
	depthStencilDesc.Format = DXGI_FORMAT_D32_FLOAT;
	depthStencilDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	depthStencilDesc.Flags = D3D12_DSV_FLAG_NONE;

	D3D12_CLEAR_VALUE depthOptimizedClearValue = {};
	depthOptimizedClearValue.Format = DXGI_FORMAT_D32_FLOAT;
	depthOptimizedClearValue.DepthStencil.Depth = 1.0f;
	depthOptimizedClearValue.DepthStencil.Stencil = 0;

	hr = device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_D32_FLOAT, Width, Height, 1, 0, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL),
		D3D12_RESOURCE_STATE_DEPTH_WRITE,
		&depthOptimizedClearValue,
		IID_PPV_ARGS(&depthStencilBuffer)
	);
	if (FAILED(hr))
	{
		Running = false;
		return false;
	}
	dsDescriptorHeap->SetName(L"Depth/Stencil Resource Heap");

	device->CreateDepthStencilView(depthStencilBuffer, &depthStencilDesc, dsDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

	// creamos el constant buffer resource heap
	// Actualizaremos el constant buffer una o mas veces por frame, por lo que solo usaremos un upload heap
	// a diferencia de otras veces usamos un upload heap para cargar los datos de vertices e indices, y luego los copiamos
	// al default heap. Para usar un resource para mas de dos frames, normalmente es mas
	// eficiente copiarlos al default heap donde permaneceran en la gpu. En este caso, nuestro constant buffer
	// sera modificado y actualizado al menos una vez por frame, por lo que solo usamos un upload heap

	// primero crearemos un resource heap (upload heap) para cada frame para los buferes de constantes de cubos
	// Estamos asignando 64 KB para cada recurso que creamos. Los buffer resource heaps deben tener
	// una alineación de 64 KB. Estamos creando 3 recursos, uno para cada frame. Cada bufder de constantes es
	// una matriz de 4x4 float. Entonces, con un float de 4 bytes, tenemos
	// 16 float en un buffer de constantes, y almacenaremos 2 buferes de constantes en cada
	// heap, uno para cada cubo, eso es solo 64x2 bits, o 128 bits que estamos usando para cada
	// recurso, y cada recurso debe tener al menos 64 KB (65536 bits) 
	for (int i = 0; i < frameBufferCount; ++i)
	{
		// creamos el resource para el cube 1
		hr = device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD), //este heap sera usado para cargar el constant buffer data
			D3D12_HEAP_FLAG_NONE, // sin flags
			&CD3DX12_RESOURCE_DESC::Buffer(1024 * 64), // tamaño del resource heap. Debe ser un multiplo de 64 KB para texturas simples y buferes constantes 
			D3D12_RESOURCE_STATE_GENERIC_READ, // seran los datos que se leeran por lo que mantendremos un estado de lectura generico
			nullptr, // no hemos utilizado un valor optimizado para los constant buffers
			IID_PPV_ARGS(&constantBufferUploadHeaps[i]));
		if (FAILED(hr))
		{
			Running = false;
			return false;
		}
		constantBufferUploadHeaps[i]->SetName(L"Constant Buffer Upload Resource Heap");

		ZeroMemory(&cbPerObject, sizeof(cbPerObject));

		CD3DX12_RANGE readRange(0, 0);	// No tenemos la intencion de leer este recurso en la CPU.

		// mapear el resource heap para obtener la gpu virtual address al inicio del heap
		hr = constantBufferUploadHeaps[i]->Map(0, &readRange, reinterpret_cast<void**>(&cbvGPUAddress[i]));

		// Debido a los requerimientos del constant read alignment, las constant buffer views deben estar alienadas a 256 bit. Nuestros buffers son mas pequeños que 256 bits,
		// por lo que necesitamos añadir espacio entre los dos buffers, por ello el segundo buffer empieza a 256 bits desde el inicio del resource heap.
		memcpy(cbvGPUAddress[i], &cbPerObject, sizeof(cbPerObject)); // constant buffer data del cubo 1
		memcpy(cbvGPUAddress[i] + ConstantBufferPerObjectAlignedSize, &cbPerObject, sizeof(cbPerObject)); // constant buffer data del cubo 2
	}

	// cargar la imagen, crear una texture resource y un descriptor heap

	// creamos el descriptor heap que se almacenara en nuestro srv
	D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
	heapDesc.NumDescriptors = 1;
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	hr = device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&mainDescriptorHeap));
	if (FAILED(hr))
	{
		Running = false;
	}

	// Cargamos la imagen desde el archivo
	D3D12_RESOURCE_DESC textureDesc;
	int imageBytesPerRow;
	BYTE* imageData;
	int imageSize = LoadImageDataFromFile(&imageData, textureDesc, L"texturaAsteroide.jpg", imageBytesPerRow);

	// nos aseguramos de tener data
	if (imageSize <= 0)
	{
		Running = false;
		return false;
	}

	// creamos un default heap donde el upload heap copiara su contenido (el contenido es la textura)
	hr = device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), // un default heap
		D3D12_HEAP_FLAG_NONE, // sin flags
		&textureDesc, // la descripcion de nuestra textura
		D3D12_RESOURCE_STATE_COPY_DEST, // copiaremos la textura desde el upload heap a aqui, por lo que empezamos en un copy dest state
		nullptr, // para los render targets y depth/stencil buffers
		IID_PPV_ARGS(&textureBuffer));
	if (FAILED(hr))
	{
		Running = false;
		return false;
	}
	textureBuffer->SetName(L"Texture Buffer Resource Heap");

	UINT64 textureUploadBufferSize;
	// esta funcion obtiene el tamaño que debe tener un upload buffer para cargar una textura a la gpu
	// cada fila debe estar alineada con 256 bytes, excepto la ultima fila, que puede ser del tamaño en bytes de la fila 
	// por ejemplo textureUploadBufferSize = ((((width * numBytesPerPixel) + 255) & ~255) * (height - 1)) + (width * numBytesPerPixel);
	// textureUploadBufferSize = (((imageBytesPerRow + 255) & ~255) * (textureDesc.Height - 1)) + imageBytesPerRow;
	device->GetCopyableFootprints(&textureDesc, 0, 1, 0, nullptr, nullptr, nullptr, &textureUploadBufferSize);

	// ahora creamos un upload heap para cargar nuestra textura a la GPU
	hr = device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD), // upload heap
		D3D12_HEAP_FLAG_NONE, // sin flags
		&CD3DX12_RESOURCE_DESC::Buffer(textureUploadBufferSize), // descripcion del recurso para un bufer (almacenando los datos de la imagen en este heap solo para copiar al default heap) 
		D3D12_RESOURCE_STATE_GENERIC_READ, // Copiaremos el contenido de este heap al default heap anterior
		nullptr,
		IID_PPV_ARGS(&textureBufferUploadHeap));
	if (FAILED(hr))
	{
		Running = false;
		return false;
	}
	textureBufferUploadHeap->SetName(L"Texture Buffer Upload Resource Heap");

	// almacenamos el vertex buffer en el upload heap
	D3D12_SUBRESOURCE_DATA textureData = {};
	textureData.pData = &imageData[0]; // puntero a nuestra image data
	textureData.RowPitch = imageBytesPerRow; // tamaño de todos los triangulos del vertex data
	textureData.SlicePitch = imageBytesPerRow * textureDesc.Height; // tamaño de todos los triangulos del vertex data

	// Ahora copiamos el contenido del upload buffer al default heap
	UpdateSubresources(commandList, textureBuffer, textureBufferUploadHeap, 0, 0, 1, &textureData);

	// transicion del texture default heap al pixel shader resource (tomaremos muestras de este heap en el pixel shader para obtener el color de los pixeles)
	commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(textureBuffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));

	// ahora creamos un shader resource view (descriptor que apunta a la textura y la describe)
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = textureDesc.Format;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;
	device->CreateShaderResourceView(textureBuffer, &srvDesc, mainDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

	// Ahora ejecutamos la command list para cargar los assets iniciales (datos del triangulo)
	commandList->Close();
	ID3D12CommandList* ppCommandLists[] = { commandList };
	commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

	// ahora incrementamos el fence value, de lo contrario es posible que el bufer no se haya cargado cuando comencemos a dibujar 
	fenceValue[frameIndex]++;
	hr = commandQueue->Signal(fence[frameIndex], fenceValue[frameIndex]);
	if (FAILED(hr))
	{
		Running = false;
		return false;
	}

	// hemos terminado con los datos de la imagen ahora que la hemos subido a la gpu, asi que liberamos
	delete imageData;

	// creamos un vertex buffer view para el triangulo. Obtenemos la direccion de memoria de la GPU al puntero de vertice usando el metodo GetGPUVirtualAddress () 
	vertexBufferView.BufferLocation = vertexBuffer->GetGPUVirtualAddress();
	vertexBufferView.StrideInBytes = sizeof(Vertex);
	vertexBufferView.SizeInBytes = vBufferSize;

	// creamos un index buffer view para el triangulo. Obtenemos la direccion de memoria de la GPU al puntero de indices usando el metodo GetGPUVirtualAddress () 
	indexBufferView.BufferLocation = indexBuffer->GetGPUVirtualAddress();
	indexBufferView.Format = DXGI_FORMAT_R32_UINT; // 32-bit unsigned integer (es un dword, double word, una word son 2 bytes)
	indexBufferView.SizeInBytes = iBufferSize;

	// Completamos la Viewport
	viewport.TopLeftX = 0;
	viewport.TopLeftY = 0;
	viewport.Width = Width;
	viewport.Height = Height;
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;

	// Completamos las scissor rect
	scissorRect.left = 0;
	scissorRect.top = 0;
	scissorRect.right = Width;
	scissorRect.bottom = Height;

	// construimos la matriz de vista y proyeccion
	XMMATRIX tmpMat = XMMatrixPerspectiveFovLH(45.0f * (3.14f / 180.0f), (float)Width / (float)Height, 0.1f, 1000.0f);
	XMStoreFloat4x4(&cameraProjMat, tmpMat);

	// establecemos el estado inicial de la camara
	cameraPosition = XMFLOAT4(0.0f, 2.0f, -4.0f, 0.0f);
	cameraTarget = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);
	cameraUp = XMFLOAT4(0.0f, 1.0f, 0.0f, 0.0f);

	// construimos la matriz de la vista
	XMVECTOR cPos = XMLoadFloat4(&cameraPosition);
	XMVECTOR cTarg = XMLoadFloat4(&cameraTarget);
	XMVECTOR cUp = XMLoadFloat4(&cameraUp);
	tmpMat = XMMatrixLookAtLH(cPos, cTarg, cUp);
	XMStoreFloat4x4(&cameraViewMat, tmpMat);

	// establcemos la posicion inicial de los cubos
	// primer cubo
	cube1Position = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f); // establecer la posicion del cubo 1
	XMVECTOR posVec = XMLoadFloat4(&cube1Position); // crear el xmvector para la posicion del cubo 1

	tmpMat = XMMatrixTranslationFromVector(posVec); // crear una matriz de traslacion a partir del vector de posicion del cubo 1 
	XMStoreFloat4x4(&cube1RotMat, XMMatrixIdentity()); // iniciar la matriz de rotacion del cubo 1 a la matriz de identidad 
	XMStoreFloat4x4(&cube1WorldMat, tmpMat); // almacenar la matriz del mundo del cubo 1

	// segundo cubo
	cube2PositionOffset = XMFLOAT4(1.5f, 0.0f, 0.0f, 0.0f);
	posVec = XMLoadFloat4(&cube2PositionOffset) + XMLoadFloat4(&cube1Position); // crear el xmvector para la posicion del cubo 2
																				// esta rotando alrededor del cubo 1, por lo que añadimos la posicion del cubo 2 al 1

	tmpMat = XMMatrixTranslationFromVector(posVec); // creamos una matriz de traduccion a partir del vector de posicion desplazado del cubo 2 
	XMStoreFloat4x4(&cube2RotMat, XMMatrixIdentity()); // iniciar la matriz de rotacion del cubo 2 a la matriz de identidad
	XMStoreFloat4x4(&cube2WorldMat, tmpMat); // almacenar la matriz del mundo del cubo 2

	return true;
}

void Update()
{
	// actualizamos la logica del juego, como mover la camara o averiguar que obejtos se incluyen en la vista

	// creamos las matrices de rotacion para el cubo 1
	XMMATRIX rotXMat = XMMatrixRotationX(0.0001f);
	XMMATRIX rotYMat = XMMatrixRotationY(0.0002f);
	XMMATRIX rotZMat = XMMatrixRotationZ(0.0003f);

	// añadimos rotacion a la matriz de rotacion del cubo 1 y la almacenamos
	XMMATRIX rotMat = XMLoadFloat4x4(&cube1RotMat) * rotXMat * rotYMat * rotZMat;
	XMStoreFloat4x4(&cube1RotMat, rotMat);

	// creamos la matriz de traslacion del cubo 1 desde el vector de posicion del cubo 1
	XMMATRIX translationMat = XMMatrixTranslationFromVector(XMLoadFloat4(&cube1Position));

	// creamos la matriz del mundo del cubo 1 girando primero el cubo, y luego posicionando el cubo girado
	XMMATRIX worldMat = rotMat * translationMat;

	// almacenamos la matriz del mundo del cubo 1
	XMStoreFloat4x4(&cube1WorldMat, worldMat);

	// actualizamos el constant buffer para el cubo 1
	// creamos la matriz wpv y la almacenamos en un constant buffer
	XMMATRIX viewMat = XMLoadFloat4x4(&cameraViewMat); // cargar la matriz vista
	XMMATRIX projMat = XMLoadFloat4x4(&cameraProjMat); // cargar la matriz de proyeccion
	XMMATRIX wvpMat = XMLoadFloat4x4(&cube1WorldMat) * viewMat * projMat; // crear la wvp matrix
	XMMATRIX transposed = XMMatrixTranspose(wvpMat); // debemos trasponer la matriz wvp para la gpu
	XMStoreFloat4x4(&cbPerObject.wvpMat, transposed); // almacenamos la matriz wvp trasposeada en el constant buffer

	// copiamos nuestra ConstantBuffer instance al mapped constant buffer resource
	memcpy(cbvGPUAddress[frameIndex], &cbPerObject, sizeof(cbPerObject));

	// matriz del mundo para el cubo2
	// creamos las matrices de rotacion para el cubo 2
	rotXMat = XMMatrixRotationX(0.0003f);
	rotYMat = XMMatrixRotationY(0.0002f);
	rotZMat = XMMatrixRotationZ(0.0001f);

	// añadimos rotacion a la matriz de rotacion del cubo 2 y la almacenamos
	rotMat = rotZMat * (XMLoadFloat4x4(&cube2RotMat) * (rotXMat * rotYMat));
	XMStoreFloat4x4(&cube2RotMat, rotMat);

	// creamos la matriz de traslacion del cubo 2 con la distancia al vector de posicion del cubo 1 (posicion relativa al cubo1)
	XMMATRIX translationOffsetMat = XMMatrixTranslationFromVector(XMLoadFloat4(&cube2PositionOffset));

	// queremos que el cubo 2 sea 1/4 de tamaño del cubo 1, por lo que lo escalamos por 1/4 en todas las dimensiones
	XMMATRIX scaleMat = XMMatrixScaling(0.25f, 0.25f, 0.25f);

	// reutilizamos worldMat.
	// primero escalamos el cubo 2. la escala ocurre en relación con el punto 0,0,0, por lo que es mejor escalar primero
	// y luego trasladarlo.
	// luego lo rotamos. la rotación siempre gira alrededor del punto 0,0,0
	// finalmente lo movemos a la posición del cubo 1, lo que hará que gire alrededor del cubo 1 
	worldMat = scaleMat * translationOffsetMat * rotMat * translationMat;

	wvpMat = XMLoadFloat4x4(&cube2WorldMat) * viewMat * projMat; // crear la wvp matrix
	transposed = XMMatrixTranspose(wvpMat); // debemos trasponer la matriz wvp para la gpu
	XMStoreFloat4x4(&cbPerObject.wvpMat, transposed); // almacenamos la matriz wvp trasposeada en el constant buffer

	// copiamos nuestra ConstantBuffer instance al constant buffer resource mapeado
	memcpy(cbvGPUAddress[frameIndex] + ConstantBufferPerObjectAlignedSize, &cbPerObject, sizeof(cbPerObject));

	// almacenamos la matriz del mundo del cubo 2
	XMStoreFloat4x4(&cube2WorldMat, worldMat);
}

void UpdatePipeline()
{
	HRESULT hr;

	// Tenemos que esperar a que la gpu termine con la command allocator antes de resetearla
	WaitForPreviousFrame();

	// solo podemos resetear un allocator una vez que la gpu haya terminado con el
	// resetear un allocator libera la memoria en la que se almaceno una command list 
	hr = commandAllocator[frameIndex]->Reset();
	if (FAILED(hr))
	{
		Running = false;
	}

	// reset la command list. 
	//al resetear la lista de comandos, la ponemos en un estado de grabacion por lo que podemos empezar a grabar comandos en el command allocator
	//El command allocator que hemos referenciado aquí podría tener multiples command lists
	// asociadas con el, pero solo una puede ser grabada en un tiempo dado.
	// Hay que asegurarse de que cualquier otra command list asociada a este command allocator este en el closed state, no en el recording.
	// Aquí pasaremos un PSO inicial como segundo parametro,
	// pero en este proyecto estamos solo clearing el rtv, aunque realmente no necesitamos mas que
	// un initial default pipeline, el cual es obtenemos al poner el segundo parametro a NULL 


	hr = commandList->Reset(commandAllocator[frameIndex], pipelineStateObject);
	if (FAILED(hr))
	{
		Running = false;
	}

	// Aqui empezamos a grabar comandos (almacenados en el command allocator) en la command list

	// Transicion del render target "frameIndex" desde el present state al render target state por lo que la command list empieza a dibujarlo a partir de aqui
	commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(renderTargets[frameIndex], D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

	// aqui obtenemos de nuevo el handle para nuestra render target wiew actual, por lo que podemos establecerlo como el render target en la etapa Output Merger del pipeline
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(rtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), frameIndex, rtvDescriptorSize);

	// obtenemos un handle para el depth/stencil buffer
	CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(dsDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

	//establecemos el render target para la Output Merger stage (el output del pipeline)
	commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

	// Clear la render target usando el comando ClearRenderTargetView
	const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
	commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);

	// clear el depth/stencil buffer
	commandList->ClearDepthStencilView(dsDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	// establecer la root signature
	commandList->SetGraphicsRootSignature(rootSignature);

	// establecer el descriptor heap
	ID3D12DescriptorHeap* descriptorHeaps[] = { mainDescriptorHeap };
	commandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	// establecer la descriptor table para el descriptor heap (parametro 1, ya que el constant buffer root descriptor es parameter index 0)
	commandList->SetGraphicsRootDescriptorTable(1, mainDescriptorHeap->GetGPUDescriptorHandleForHeapStart());

	commandList->RSSetViewports(1, &viewport); // establecer las viewports
	commandList->RSSetScissorRects(1, &scissorRect); // establecer las scissor rects
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST); // establecer la primitive topology
	commandList->IASetVertexBuffers(0, 1, &vertexBufferView); // establecer el vertex buffer (usando la vertex buffer view)
	commandList->IASetIndexBuffer(&indexBufferView);

	// primer cubo

	// establecer el constant buffer del cubo 1
	commandList->SetGraphicsRootConstantBufferView(0, constantBufferUploadHeaps[frameIndex]->GetGPUVirtualAddress());

	// dibujar el primer cubo
	commandList->DrawIndexedInstanced(numCubeIndices, 1, 0, 0, 0);

	// segundo cubo

	// establecer el constant buffer del cubo 2. Añadimos el tamaño del ConstantBufferPerObject al constant buffer
	// resource heaps address. Esto se debe a que el constant buffer del ubo 1 esta almacenado al principio del resource heap, mientras que
	// el constant buffer data del cubo 2 esta almacenado despues (256 bits desde el inicio hasta el heap).
	commandList->SetGraphicsRootConstantBufferView(0, constantBufferUploadHeaps[frameIndex]->GetGPUVirtualAddress() + ConstantBufferPerObjectAlignedSize);

	// dibujar el segundo cubo
	commandList->DrawIndexedInstanced(numCubeIndices, 1, 0, 0, 0);

	//transicion del "frameIndex" render target desde el render target state al present state. Si debug layer está activada, 
	//recibiremos un warning si el present es llamado en el render target cuando no esta en el present state
	commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(renderTargets[frameIndex], D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

	hr = commandList->Close();
	if (FAILED(hr))
	{
		Running = false;
	}
}

void Render()
{
	HRESULT hr;

	UpdatePipeline(); // actualiza el pipeline enviando commands a la commandqueue

	// creamos un array de command lists (solo una command list aqui)
	ID3D12CommandList* ppCommandLists[] = { commandList };

	// ejecutamos el array de command lists
	commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

	// este coamndo va al final de nuestra command queue. Sabremos cuando nuestra command queue
	//ha terminado porque el valor de la fence se establecera a "fenceValue" por la GPU desde que
	//la cola sea ejecutada por la GPU
	hr = commandQueue->Signal(fence[frameIndex], fenceValue[frameIndex]);
	if (FAILED(hr))
	{
		Running = false;
	}

	// present del current backbuffer
	hr = swapChain->Present(0, 0);
	if (FAILED(hr))
	{
		Running = false;
	}
}

void Cleanup()
{
	// esperar a que la GPU termine todos los frames
	for (int i = 0; i < frameBufferCount; ++i)
	{
		frameIndex = i;
		WaitForPreviousFrame();
	}

	// sacar la swapchain del fullscreen antes de salir
	BOOL fs = false;
	if (swapChain->GetFullscreenState(&fs, NULL))
		swapChain->SetFullscreenState(false, NULL);

	SAFE_RELEASE(device);
	SAFE_RELEASE(swapChain);
	SAFE_RELEASE(commandQueue);
	SAFE_RELEASE(rtvDescriptorHeap);
	SAFE_RELEASE(commandList);

	for (int i = 0; i < frameBufferCount; ++i)
	{
		SAFE_RELEASE(renderTargets[i]);
		SAFE_RELEASE(commandAllocator[i]);
		SAFE_RELEASE(fence[i]);
	};

	SAFE_RELEASE(pipelineStateObject);
	SAFE_RELEASE(rootSignature);
	SAFE_RELEASE(vertexBuffer);
	SAFE_RELEASE(indexBuffer);

	SAFE_RELEASE(depthStencilBuffer);
	SAFE_RELEASE(dsDescriptorHeap);

	for (int i = 0; i < frameBufferCount; ++i)
	{
		SAFE_RELEASE(constantBufferUploadHeaps[i]);
	};
}

void WaitForPreviousFrame()
{
	HRESULT hr;

	// intercambia el indice de bufer rtv actual para que dibujemos en el bufer correcto
	frameIndex = swapChain->GetCurrentBackBufferIndex();

	// si el valor de la actual fence es todavia menor que el "fence Value", sabemos que la GPU no ha terminado de ejecutar
	// la command queue, ya que no ha alcanzado el comando "commandQueue->Signal(fence, fenceValue)"
	if (fence[frameIndex]->GetCompletedValue() < fenceValue[frameIndex])
	{
		// hacemos que la fence cree un evento que señalice cuando el valor actual de la fence es el "fenceValue"
		hr = fence[frameIndex]->SetEventOnCompletion(fenceValue[frameIndex], fenceEvent);
		if (FAILED(hr))
		{
			Running = false;
		}

		// Tenemos que esperar hasta que la fence haya "triggeado" el evento en el que su valor haya alzanzado el "fenceValue"
		// De esta forma sabremos que la command queue ha terminado de ejecutar
		WaitForSingleObject(fenceEvent, INFINITE);
	}

	// incrementa el fenceValue para el siguiente frame
	fenceValue[frameIndex]++;
}

// obtenemos el foramto dxgi equivalente al foramto wic
DXGI_FORMAT GetDXGIFormatFromWICFormat(WICPixelFormatGUID& wicFormatGUID)
{
	if (wicFormatGUID == GUID_WICPixelFormat128bppRGBAFloat) return DXGI_FORMAT_R32G32B32A32_FLOAT;
	else if (wicFormatGUID == GUID_WICPixelFormat64bppRGBAHalf) return DXGI_FORMAT_R16G16B16A16_FLOAT;
	else if (wicFormatGUID == GUID_WICPixelFormat64bppRGBA) return DXGI_FORMAT_R16G16B16A16_UNORM;
	else if (wicFormatGUID == GUID_WICPixelFormat32bppRGBA) return DXGI_FORMAT_R8G8B8A8_UNORM;
	else if (wicFormatGUID == GUID_WICPixelFormat32bppBGRA) return DXGI_FORMAT_B8G8R8A8_UNORM;
	else if (wicFormatGUID == GUID_WICPixelFormat32bppBGR) return DXGI_FORMAT_B8G8R8X8_UNORM;
	else if (wicFormatGUID == GUID_WICPixelFormat32bppRGBA1010102XR) return DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM;

	else if (wicFormatGUID == GUID_WICPixelFormat32bppRGBA1010102) return DXGI_FORMAT_R10G10B10A2_UNORM;
	else if (wicFormatGUID == GUID_WICPixelFormat16bppBGRA5551) return DXGI_FORMAT_B5G5R5A1_UNORM;
	else if (wicFormatGUID == GUID_WICPixelFormat16bppBGR565) return DXGI_FORMAT_B5G6R5_UNORM;
	else if (wicFormatGUID == GUID_WICPixelFormat32bppGrayFloat) return DXGI_FORMAT_R32_FLOAT;
	else if (wicFormatGUID == GUID_WICPixelFormat16bppGrayHalf) return DXGI_FORMAT_R16_FLOAT;
	else if (wicFormatGUID == GUID_WICPixelFormat16bppGray) return DXGI_FORMAT_R16_UNORM;
	else if (wicFormatGUID == GUID_WICPixelFormat8bppGray) return DXGI_FORMAT_R8_UNORM;
	else if (wicFormatGUID == GUID_WICPixelFormat8bppAlpha) return DXGI_FORMAT_A8_UNORM;

	else return DXGI_FORMAT_UNKNOWN;
}

// obtener un dxgi compatible wic format desde otro wic format
WICPixelFormatGUID GetConvertToWICFormat(WICPixelFormatGUID& wicFormatGUID)
{
	if (wicFormatGUID == GUID_WICPixelFormatBlackWhite) return GUID_WICPixelFormat8bppGray;
	else if (wicFormatGUID == GUID_WICPixelFormat1bppIndexed) return GUID_WICPixelFormat32bppRGBA;
	else if (wicFormatGUID == GUID_WICPixelFormat2bppIndexed) return GUID_WICPixelFormat32bppRGBA;
	else if (wicFormatGUID == GUID_WICPixelFormat4bppIndexed) return GUID_WICPixelFormat32bppRGBA;
	else if (wicFormatGUID == GUID_WICPixelFormat8bppIndexed) return GUID_WICPixelFormat32bppRGBA;
	else if (wicFormatGUID == GUID_WICPixelFormat2bppGray) return GUID_WICPixelFormat8bppGray;
	else if (wicFormatGUID == GUID_WICPixelFormat4bppGray) return GUID_WICPixelFormat8bppGray;
	else if (wicFormatGUID == GUID_WICPixelFormat16bppGrayFixedPoint) return GUID_WICPixelFormat16bppGrayHalf;
	else if (wicFormatGUID == GUID_WICPixelFormat32bppGrayFixedPoint) return GUID_WICPixelFormat32bppGrayFloat;
	else if (wicFormatGUID == GUID_WICPixelFormat16bppBGR555) return GUID_WICPixelFormat16bppBGRA5551;
	else if (wicFormatGUID == GUID_WICPixelFormat32bppBGR101010) return GUID_WICPixelFormat32bppRGBA1010102;
	else if (wicFormatGUID == GUID_WICPixelFormat24bppBGR) return GUID_WICPixelFormat32bppRGBA;
	else if (wicFormatGUID == GUID_WICPixelFormat24bppRGB) return GUID_WICPixelFormat32bppRGBA;
	else if (wicFormatGUID == GUID_WICPixelFormat32bppPBGRA) return GUID_WICPixelFormat32bppRGBA;
	else if (wicFormatGUID == GUID_WICPixelFormat32bppPRGBA) return GUID_WICPixelFormat32bppRGBA;
	else if (wicFormatGUID == GUID_WICPixelFormat48bppRGB) return GUID_WICPixelFormat64bppRGBA;
	else if (wicFormatGUID == GUID_WICPixelFormat48bppBGR) return GUID_WICPixelFormat64bppRGBA;
	else if (wicFormatGUID == GUID_WICPixelFormat64bppBGRA) return GUID_WICPixelFormat64bppRGBA;
	else if (wicFormatGUID == GUID_WICPixelFormat64bppPRGBA) return GUID_WICPixelFormat64bppRGBA;
	else if (wicFormatGUID == GUID_WICPixelFormat64bppPBGRA) return GUID_WICPixelFormat64bppRGBA;
	else if (wicFormatGUID == GUID_WICPixelFormat48bppRGBFixedPoint) return GUID_WICPixelFormat64bppRGBAHalf;
	else if (wicFormatGUID == GUID_WICPixelFormat48bppBGRFixedPoint) return GUID_WICPixelFormat64bppRGBAHalf;
	else if (wicFormatGUID == GUID_WICPixelFormat64bppRGBAFixedPoint) return GUID_WICPixelFormat64bppRGBAHalf;
	else if (wicFormatGUID == GUID_WICPixelFormat64bppBGRAFixedPoint) return GUID_WICPixelFormat64bppRGBAHalf;
	else if (wicFormatGUID == GUID_WICPixelFormat64bppRGBFixedPoint) return GUID_WICPixelFormat64bppRGBAHalf;
	else if (wicFormatGUID == GUID_WICPixelFormat64bppRGBHalf) return GUID_WICPixelFormat64bppRGBAHalf;
	else if (wicFormatGUID == GUID_WICPixelFormat48bppRGBHalf) return GUID_WICPixelFormat64bppRGBAHalf;
	else if (wicFormatGUID == GUID_WICPixelFormat128bppPRGBAFloat) return GUID_WICPixelFormat128bppRGBAFloat;
	else if (wicFormatGUID == GUID_WICPixelFormat128bppRGBFloat) return GUID_WICPixelFormat128bppRGBAFloat;
	else if (wicFormatGUID == GUID_WICPixelFormat128bppRGBAFixedPoint) return GUID_WICPixelFormat128bppRGBAFloat;
	else if (wicFormatGUID == GUID_WICPixelFormat128bppRGBFixedPoint) return GUID_WICPixelFormat128bppRGBAFloat;
	else if (wicFormatGUID == GUID_WICPixelFormat32bppRGBE) return GUID_WICPixelFormat128bppRGBAFloat;
	else if (wicFormatGUID == GUID_WICPixelFormat32bppCMYK) return GUID_WICPixelFormat32bppRGBA;
	else if (wicFormatGUID == GUID_WICPixelFormat64bppCMYK) return GUID_WICPixelFormat64bppRGBA;
	else if (wicFormatGUID == GUID_WICPixelFormat40bppCMYKAlpha) return GUID_WICPixelFormat64bppRGBA;
	else if (wicFormatGUID == GUID_WICPixelFormat80bppCMYKAlpha) return GUID_WICPixelFormat64bppRGBA;

#if (_WIN32_WINNT >= _WIN32_WINNT_WIN8) || defined(_WIN7_PLATFORM_UPDATE)
	else if (wicFormatGUID == GUID_WICPixelFormat32bppRGB) return GUID_WICPixelFormat32bppRGBA;
	else if (wicFormatGUID == GUID_WICPixelFormat64bppRGB) return GUID_WICPixelFormat64bppRGBA;
	else if (wicFormatGUID == GUID_WICPixelFormat64bppPRGBAHalf) return GUID_WICPixelFormat64bppRGBAHalf;
#endif

	else return GUID_WICPixelFormatDontCare;
}

// obtener el numero de bits por pixel para un foramto dxgi 
int GetDXGIFormatBitsPerPixel(DXGI_FORMAT& dxgiFormat)
{
	if (dxgiFormat == DXGI_FORMAT_R32G32B32A32_FLOAT) return 128;
	else if (dxgiFormat == DXGI_FORMAT_R16G16B16A16_FLOAT) return 64;
	else if (dxgiFormat == DXGI_FORMAT_R16G16B16A16_UNORM) return 64;
	else if (dxgiFormat == DXGI_FORMAT_R8G8B8A8_UNORM) return 32;
	else if (dxgiFormat == DXGI_FORMAT_B8G8R8A8_UNORM) return 32;
	else if (dxgiFormat == DXGI_FORMAT_B8G8R8X8_UNORM) return 32;
	else if (dxgiFormat == DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM) return 32;

	else if (dxgiFormat == DXGI_FORMAT_R10G10B10A2_UNORM) return 32;
	else if (dxgiFormat == DXGI_FORMAT_B5G5R5A1_UNORM) return 16;
	else if (dxgiFormat == DXGI_FORMAT_B5G6R5_UNORM) return 16;
	else if (dxgiFormat == DXGI_FORMAT_R32_FLOAT) return 32;
	else if (dxgiFormat == DXGI_FORMAT_R16_FLOAT) return 16;
	else if (dxgiFormat == DXGI_FORMAT_R16_UNORM) return 16;
	else if (dxgiFormat == DXGI_FORMAT_R8_UNORM) return 8;
	else if (dxgiFormat == DXGI_FORMAT_A8_UNORM) return 8;
}

// cargar y decodificar una imagen desde un archivo 
int LoadImageDataFromFile(BYTE** imageData, D3D12_RESOURCE_DESC& resourceDescription, LPCWSTR filename, int& bytesPerRow)
{
	HRESULT hr;

	// solo necesitamos una instancia de la imaging factory para crear decoders y frames
	static IWICImagingFactory* wicFactory;

	// resetear el decodificador, el frame y el converter, desde que sean diferentes para cada imagen que carguemos 
	IWICBitmapDecoder* wicDecoder = NULL;
	IWICBitmapFrameDecode* wicFrame = NULL;
	IWICFormatConverter* wicConverter = NULL;

	bool imageConverted = false;

	if (wicFactory == NULL)
	{
		// Inicializar la COM library
		CoInitialize(NULL);

		// crear la WIC factory
		hr = CoCreateInstance(
			CLSID_WICImagingFactory,
			NULL,
			CLSCTX_INPROC_SERVER,
			IID_PPV_ARGS(&wicFactory)
		);
		if (FAILED(hr)) return 0;

		hr = wicFactory->CreateFormatConverter(&wicConverter);
		if (FAILED(hr)) return 0;
	}

	// cargar un decoder para la imagen
	hr = wicFactory->CreateDecoderFromFilename(
		filename,                        // Imagen que queremos cargar
		NULL,                            // Este es uel ID del proveedor, no queremos uno especifico, asi lo ponemos en nulo 
		GENERIC_READ,                    // Queremos leer desde este archivo
		WICDecodeMetadataCacheOnLoad,    // Almacenamos en cache los metadatos de inmediato, en lugar de cuando sea necesario, que puede ser desconocido 
		&wicDecoder                      // el wic decoder para crear
	);
	if (FAILED(hr)) return 0;

	// obtener la imagen desde el decoder (esto decodificara el frame)
	hr = wicDecoder->GetFrame(0, &wicFrame);
	if (FAILED(hr)) return 0;

	// obtener el wic pixel format de la imagen
	WICPixelFormatGUID pixelFormat;
	hr = wicFrame->GetPixelFormat(&pixelFormat);
	if (FAILED(hr)) return 0;

	// obtener el tamaño de la imagen
	UINT textureWidth, textureHeight;
	hr = wicFrame->GetSize(&textureWidth, &textureHeight);
	if (FAILED(hr)) return 0;

	// no estamos manejando tipos sRGB

	// convertir el wic pixel format al dxgi pixel format
	DXGI_FORMAT dxgiFormat = GetDXGIFormatFromWICFormat(pixelFormat);

	// si el formato de la imagen no es un formato dxgi compatible, intentamos convertirlo
	if (dxgiFormat == DXGI_FORMAT_UNKNOWN)
	{
		// obtenemos un dxgi compatible wic format desde el formato actual de la imagen
		WICPixelFormatGUID convertToPixelFormat = GetConvertToWICFormat(pixelFormat);

		// si no se encuentra un dxgi compatible format
		if (convertToPixelFormat == GUID_WICPixelFormatDontCare) return 0;

		// establecer el dxgi format
		dxgiFormat = GetDXGIFormatFromWICFormat(convertToPixelFormat);

		// asegurarse de que podemos convertir a un  formato dxgi compatible
		BOOL canConvert = FALSE;
		hr = wicConverter->CanConvert(pixelFormat, convertToPixelFormat, &canConvert);
		if (FAILED(hr) || !canConvert) return 0;

		// hacer la conversion (wicConverter contendra la imagen convertida)
		hr = wicConverter->Initialize(wicFrame, convertToPixelFormat, WICBitmapDitherTypeErrorDiffusion, 0, 0, WICBitmapPaletteTypeCustom);
		if (FAILED(hr)) return 0;

		// esto es para obtener los datos de la imagen del wicConverter (de lo contrario, los obtendremos de wicFrame) 
		imageConverted = true;
	}

	int bitsPerPixel = GetDXGIFormatBitsPerPixel(dxgiFormat); // numero de bits por pixel
	bytesPerRow = (textureWidth * bitsPerPixel) / 8; // numero de bytes en cada fila de imagen de datos
	int imageSize = bytesPerRow * textureHeight; // tamaño total de la imagen en bytes

	// asignar suficiente memoria para los datos de imagen raw y configurar imageData para apuntar a esa memoria 
	*imageData = (BYTE*)malloc(imageSize);

	// copiar (decodificar) los datos de imagen raw en la memoria recien asignada (imageData) 
	if (imageConverted)
	{
		// si el formato de imagen necesita ser vonvertido, el wic converter contendra la imagen convertida
		hr = wicConverter->CopyPixels(0, bytesPerRow, imageSize, *imageData);
		if (FAILED(hr)) return 0;
	}
	else
	{
		// no es necesario convertir, solo copiar los datos del wic frame
		hr = wicFrame->CopyPixels(0, bytesPerRow, imageSize, *imageData);
		if (FAILED(hr)) return 0;
	}

	// ahora describir la textura con la informacion que obtenemos de la imagen
	resourceDescription = {};
	resourceDescription.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	resourceDescription.Alignment = 0; // puede ser 0, 4KB, 64KB, o 4MB. 0 permitira que el tiempo de ejecucion decida entre 64 KB y 4 MB (4 MB para texturas de multiples muestras) 
	resourceDescription.Width = textureWidth; // ancho de la textura
	resourceDescription.Height = textureHeight; // alto de la textura
	resourceDescription.DepthOrArraySize = 1; // si es una imagen en 3D, profundidad de la imagen en 3D. De lo contrario, un array de texturas 1D o 2D (solo tenemos una imagen, por lo que establecemos 1) 
	resourceDescription.MipLevels = 1; // Numero de mipmaps. No estamos generando mipmaps para esta textura, por lo que solo tenemos un nivel 
	resourceDescription.Format = dxgiFormat; // Este es el formato dxgi de la imagen (formato de los pixeles)
	resourceDescription.SampleDesc.Count = 1; // Este es el numero de samples por pixel, solo queremos 1 sample
	resourceDescription.SampleDesc.Quality = 0; // El nivel de calidad de las samples. Cuanto mas alta, mejor calidad, pero peor rendimiento.
	resourceDescription.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN; // La disposicion de los pixeles. Establecer como desconocido permite elegir el mas eficiente 
	resourceDescription.Flags = D3D12_RESOURCE_FLAG_NONE; // sin flags

	// devolver el tamaño de la imagen. recordar eliminar la imagen una vez que se termine con ella (una vez que se cargo en la gpu) 
	return imageSize;
}
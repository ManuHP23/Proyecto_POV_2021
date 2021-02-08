#include "stdafx.h"

struct Vertex {
	Vertex(float x, float y, float z, float r, float g, float b, float a) : pos(x, y, z), color(r, g, b, a) {}
	XMFLOAT3 pos;
	XMFLOAT4 color;
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
		return false;
	}

	// -- Crear la direct command queue -- //

	D3D12_COMMAND_QUEUE_DESC cqDesc = {};
	cqDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	cqDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT; // direct significa que la gpu puede ejecutar directamente la command queue

	hr = device->CreateCommandQueue(&cqDesc, IID_PPV_ARGS(&commandQueue)); // crear la command queue
	if (FAILED(hr))
	{
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
			return false;
		}
	}

	// -- Crear la Command List -- //

	// crea la command list con el primer allocator
	hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator[0], NULL, IID_PPV_ARGS(&commandList));
	if (FAILED(hr))
	{
		return false;
	}

	// -- Crear una Fence & Fence Event -- //

	// crear las fences
	for (int i = 0; i < frameBufferCount; i++)
	{
		hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence[i]));
		if (FAILED(hr))
		{
			return false;
		}
		fenceValue[i] = 0; // establecemos el valor inicial de la fence a 0
	}

	// creamos un handle para un fence event
	fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	if (fenceEvent == nullptr)
	{
		return false;
	}

	// creamos la root signature

	// creamos un root descriptor, que explica donde encontrar los datos para este root parameter
	D3D12_ROOT_DESCRIPTOR rootCBVDescriptor;
	rootCBVDescriptor.RegisterSpace = 0;
	rootCBVDescriptor.ShaderRegister = 0;

	// craeamos un root parameter y lo completamos
	D3D12_ROOT_PARAMETER  rootParameters[1]; // solo un parametro
	rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV; // esta es una constant buffer view root descriptor
	rootParameters[0].Descriptor = rootCBVDescriptor; // esta es la root descriptor para este root parameter
	rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX; // nuestro pixel shader sera el unico shader que acceda a este parametro por ahora

	CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc;
	rootSignatureDesc.Init(_countof(rootParameters), // tenemos un root parameter
		rootParameters, // un puntero hacia el inicio de nuestro root parameters array
		0,
		nullptr,
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT | // podemos negar etapas del sombreado para un mejor rendimiento
		D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS);

	ID3DBlob* signature;
	hr = D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, nullptr);
	if (FAILED(hr))
	{
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
	ID3DBlob* errorBuff; // un buffer que contiene los errores de datos si los hubiera
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
		{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
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
		return false;
	}

	// Creamos el vertex buffer

	// un cuadrado
	Vertex vList[] = {
		// cara frontal
		{ -0.5f,  0.5f, -0.5f, 1.0f, 0.0f, 0.0f, 1.0f },
		{  0.5f, -0.5f, -0.5f, 1.0f, 0.0f, 1.0f, 1.0f },
		{ -0.5f, -0.5f, -0.5f, 0.0f, 0.0f, 1.0f, 1.0f },
		{  0.5f,  0.5f, -0.5f, 0.0f, 1.0f, 0.0f, 1.0f },

		// cara lateral derecha
		{  0.5f, -0.5f, -0.5f, 1.0f, 0.0f, 0.0f, 1.0f },
		{  0.5f,  0.5f,  0.5f, 1.0f, 0.0f, 1.0f, 1.0f },
		{  0.5f, -0.5f,  0.5f, 0.0f, 0.0f, 1.0f, 1.0f },
		{  0.5f,  0.5f, -0.5f, 0.0f, 1.0f, 0.0f, 1.0f },

		// cara lateral izquierda
		{ -0.5f,  0.5f,  0.5f, 1.0f, 0.0f, 0.0f, 1.0f },
		{ -0.5f, -0.5f, -0.5f, 1.0f, 0.0f, 1.0f, 1.0f },
		{ -0.5f, -0.5f,  0.5f, 0.0f, 0.0f, 1.0f, 1.0f },
		{ -0.5f,  0.5f, -0.5f, 0.0f, 1.0f, 0.0f, 1.0f },

		// cara trasera
		{  0.5f,  0.5f,  0.5f, 1.0f, 0.0f, 0.0f, 1.0f },
		{ -0.5f, -0.5f,  0.5f, 1.0f, 0.0f, 1.0f, 1.0f },
		{  0.5f, -0.5f,  0.5f, 0.0f, 0.0f, 1.0f, 1.0f },
		{ -0.5f,  0.5f,  0.5f, 0.0f, 1.0f, 0.0f, 1.0f },

		// cara superior
		{ -0.5f,  0.5f, -0.5f, 1.0f, 0.0f, 0.0f, 1.0f },
		{ 0.5f,  0.5f,  0.5f, 1.0f, 0.0f, 1.0f, 1.0f },
		{ 0.5f,  0.5f, -0.5f, 0.0f, 0.0f, 1.0f, 1.0f },
		{ -0.5f,  0.5f,  0.5f, 0.0f, 1.0f, 0.0f, 1.0f },

		// cara inferior
		{  0.5f, -0.5f,  0.5f, 1.0f, 0.0f, 0.0f, 1.0f },
		{ -0.5f, -0.5f, -0.5f, 1.0f, 0.0f, 1.0f, 1.0f },
		{  0.5f, -0.5f, -0.5f, 0.0f, 0.0f, 1.0f, 1.0f },
		{ -0.5f, -0.5f,  0.5f, 0.0f, 1.0f, 0.0f, 1.0f },
	};

	int vBufferSize = sizeof(vList);

	// creamos el default heap
	// el default heap es memoria en la GPU. Solo la GPU tiene acceso a esta memoria
	// Para obtener datos en este heap, tendremos que cargar los datos usando
	// un upload heap
	device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), // un default heap
		D3D12_HEAP_FLAG_NONE, // sin flags
		&CD3DX12_RESOURCE_DESC::Buffer(vBufferSize), // descripcion del recurso para el buffer
		D3D12_RESOURCE_STATE_COPY_DEST, // iniciaremos este heap en el estado de destino de la copia ya que copiaremos los datos
										// desde el upload heap hasta este heap
		nullptr, // El valor optimizado debe ser nulo para este tipo de recursos usado para render targets y depth/stencil buffers
		IID_PPV_ARGS(&vertexBuffer));

	// Podemos poner nombre a los resource heaps para que, cuando depuramos con el depurador de gráficos, sepamos que recurso estamos buscando 
	vertexBuffer->SetName(L"Vertex Buffer Resource Heap");

	// creamos el upload heap
	// los upload heaps son usados para cargar datos a la GPU. La CPU puede escribir en ella. La GPU puede leer en ella.
	// Cargaremos el vertex buffer usando este heap para el default heap
	ID3D12Resource* vBufferUploadHeap;
	device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD), // upload heap
		D3D12_HEAP_FLAG_NONE, // sin flags
		&CD3DX12_RESOURCE_DESC::Buffer(vBufferSize), // descripcion del recurso para el buffer
		D3D12_RESOURCE_STATE_GENERIC_READ, // La GPU leera desde este buffer y copiara su contenido al default heap
		nullptr,
		IID_PPV_ARGS(&vBufferUploadHeap));
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
	device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), // el default heap
		D3D12_HEAP_FLAG_NONE, // sin flags
		&CD3DX12_RESOURCE_DESC::Buffer(iBufferSize), // descripcion de recurso para un buffer
		D3D12_RESOURCE_STATE_COPY_DEST, // empezar en el copy destination state
		nullptr, // el valor optimizado debe ser nulo para este tipo de recurso 
		IID_PPV_ARGS(&indexBuffer));

	// podemos darle a los resource heaps un nombre. Asi cuando utilicemos el debug con el graphics debugger sabremos el recurso que estamos analizando
	vertexBuffer->SetName(L"Index Buffer Resource Heap");

	// creamos el upload heap para cargar el index buffer
	ID3D12Resource* iBufferUploadHeap;
	device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD), // upload heap
		D3D12_HEAP_FLAG_NONE, // sin flags
		&CD3DX12_RESOURCE_DESC::Buffer(vBufferSize), // descripcion de recurso para un buffer
		D3D12_RESOURCE_STATE_GENERIC_READ, // la GPU leera desde este buffer y copiara su contenido al default heap
		nullptr,
		IID_PPV_ARGS(&iBufferUploadHeap));
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
	}

	D3D12_DEPTH_STENCIL_VIEW_DESC depthStencilDesc = {};
	depthStencilDesc.Format = DXGI_FORMAT_D32_FLOAT;
	depthStencilDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	depthStencilDesc.Flags = D3D12_DSV_FLAG_NONE;

	D3D12_CLEAR_VALUE depthOptimizedClearValue = {};
	depthOptimizedClearValue.Format = DXGI_FORMAT_D32_FLOAT;
	depthOptimizedClearValue.DepthStencil.Depth = 1.0f;
	depthOptimizedClearValue.DepthStencil.Stencil = 0;

	device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_D32_FLOAT, Width, Height, 1, 0, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL),
		D3D12_RESOURCE_STATE_DEPTH_WRITE,
		&depthOptimizedClearValue,
		IID_PPV_ARGS(&depthStencilBuffer)
		);
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
		constantBufferUploadHeaps[i]->SetName(L"Constant Buffer Upload Resource Heap");

		ZeroMemory(&cbPerObject, sizeof(cbPerObject));

		CD3DX12_RANGE readRange(0, 0);	// No tenemos la intencion de leer este recurso en la CPU.
		
		// mapear el resource heap para obtener la gpu virtual address al inicio del heap
		hr = constantBufferUploadHeaps[i]->Map(0, &readRange, reinterpret_cast<void**>(&cbvGPUAddress[i]));

		// Debido a los requerimientos del constant read alignment, las constant buffer views deben estar alienadas a 256 bit. Nuestros buffers son mas pequeños que 256 bits,
		// por lo que necesitamos añadir espacio entre los dos buffers, por ello el segundo buffer empieza a 256 bits desde el inicio del resource heap.
		memcpy(cbvGPUAddress[i], &cbPerObject, sizeof(cbPerObject)); // constant buffer data del cubo 1
		memcpy(cbvGPUAddress[i] + ConstantBufferPerObjectAlignedSize, &cbPerObject, sizeof(cbPerObject)); // onstant buffer data del cubo 2
	}

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
	}

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
	commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

	// Clear la render target usando el comando ClearRenderTargetView
	const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
	commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);

	// clear el depth/stencil buffer
	commandList->ClearDepthStencilView(dsDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	// establecer la root signature
	commandList->SetGraphicsRootSignature(rootSignature); // establecer la root signature
	
	// dibujar un triangulo
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
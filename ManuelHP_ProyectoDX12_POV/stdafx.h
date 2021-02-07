#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers.
#endif

#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <D3Dcompiler.h>
#include <DirectXMath.h>
#include "d3dx12.h"
#include <string>

// solo llamara release si exite un objeto (previene la llamada al release de objetos no existentes)
#define SAFE_RELEASE(p) { if ( (p) ) { (p)->Release(); (p) = 0; } }

// identificador de la ventana
HWND hwnd = NULL;

// nombre de la ventana (no el titulo)
LPCTSTR WindowName = L"Proyecto Final POV";

// titulo de la ventana
LPCTSTR WindowTitle = L"Proyecto POV";

// ancho y alto de la ventana
int Width = 800;
int Height = 600;

// fullscreen?
bool FullScreen = false;

// saldremos de la aplicacion cuando es false
bool Running = true;

// crear la ventana
bool InitializeWindow(HINSTANCE hInstance,
	int ShowWnd,
	bool fullscreen);

// main loop de la aplicacion
void mainloop();

// callback function windows messages
LRESULT CALLBACK WndProc(HWND hWnd,
	UINT msg,
	WPARAM wParam,
	LPARAM lParam);

// direct3d stuff
const int frameBufferCount = 3; // numero de buffers que queremos, 2 para double buffering, 3 para tripple buffering

ID3D12Device* device; // dispositivo direct3d 

IDXGISwapChain3* swapChain; // swapchain usada para intercambiar entre diferentes render targets

ID3D12CommandQueue* commandQueue; // container para command lists

ID3D12DescriptorHeap* rtvDescriptorHeap; // descriptor heap para manejar recursos como los render targets

ID3D12Resource* renderTargets[frameBufferCount]; // numero de render targets igual al numero de buffers

ID3D12CommandAllocator* commandAllocator[frameBufferCount]; // queremos suficientes allocators para cada buffer * numero de threads (solo tenemos un thread)

ID3D12GraphicsCommandList* commandList; // una command list donde podemos guardar commands, para luego ejecutarlos en el frame de renderizado

ID3D12Fence* fence[frameBufferCount];    // un objeto que esta bloqueado mientras nuestra command list esta siendo ejecutada por la gpu. Necesitamos 
										 //tantos como allocators tengamos (mas si queremos saber cuando la gpu termina con un asset)

HANDLE fenceEvent; // un identificador para un evento cuando nuestra fence esta desbloqueda por la gpu

UINT64 fenceValue[frameBufferCount]; // este valor se incrementa cada frame. cada fence tendra su propio valor

int frameIndex; // rtv actual en el que estamos

int rtvDescriptorSize; // tamaño del descritor rtv del dispositivo (todos los front y back buffers seran del mismo tamaño)

					   // declaracion de funciones
bool InitD3D(); // inicializar direct3d 12

void Update(); // actualizar logica del juego

void UpdatePipeline(); // actualizar el direct3d pipeline (actualizar command lists)

void Render(); // ejecutar la command list

void Cleanup(); // liberar com ojects y limpiar la memoria

void WaitForPreviousFrame(); // esperar hasta que la gpu termine con la commnad list

ID3D12PipelineState* pipelineStateObject; // pso contiene un estado del pipeline

ID3D12RootSignature* rootSignature; // root signature define los datos a los que accderan los shaders shaders

D3D12_VIEWPORT viewport; // area del render target sobre la que se dibuja la escena

D3D12_RECT scissorRect; // el area a dibujar. Los pixeles fuera de esa area no se dibujaran

ID3D12Resource* vertexBuffer; // un default buffer en la memoria de la GPU en la que cargaremos los datos de vertices para nuestro triangulo
ID3D12Resource* indexBuffer; // un default buffer en la memoria de la GPU en el que cargaremos datos de indices para nuestro triangulo 


D3D12_VERTEX_BUFFER_VIEW vertexBufferView; // una estructura que contiene un puntero a los datos del vertice en la memoria de la gpu
											// el tamaño total del bufer y el tamaño de cada elemento (vertice) 
D3D12_INDEX_BUFFER_VIEW indexBufferView; // una estructura que contiene informacion sobre el buffer de indices
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

using namespace DirectX; // usaremos la directxmath library

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

ID3D12Resource* depthStencilBuffer; // Esta es la memoria de nuestro depth buffer. Tambien se usara para el stencil buffer 
ID3D12DescriptorHeap* dsDescriptorHeap; // Este es un heap para nuestro depth/stencil buffer descriptor

// esta es la estructura de nuestro constant buffer.
struct ConstantBufferPerObject {
	XMFLOAT4X4 wvpMat;
};

// Los buferes de constantes deben estar alineados con 256 bytes, algo que tiene que ver con lecturas constantes en la GPU.
// Solo podemos leer a intervalos de 256 bytes desde el inicio de un resource heap, por lo que
// hay que asegurarse de agregar contenido entre los dos bufers de constantes en el heap (uno para cubo 1 y otro para el cubo 2).
// No lo hacemos porque en realidad se desperdiciarian ciclos de CPU cuando memcpy nuestra constante
// almacena los datos en la direccion virtual de la gpu. Actualmente memorizamos el tamaño de nuestra estructura.
int ConstantBufferPerObjectAlignedSize = (sizeof(ConstantBufferPerObject) + 255) & ~255;

ConstantBufferPerObject cbPerObject; // este es el constant buffer data que enviaremos a la gpu 
										// (y que sera colocado en el resource creado)

ID3D12Resource* constantBufferUploadHeaps[frameBufferCount]; // memoria de la GPU donde se alojan nuestros constant buffers para cada frame

UINT8* cbvGPUAddress[frameBufferCount]; // este es un puntero para cada constant buffer resource heaps

XMFLOAT4X4 cameraProjMat; // para almacenar nuestra projection matrix
XMFLOAT4X4 cameraViewMat; // para almacenar nuestra view matrix

XMFLOAT4 cameraPosition; // vector de posicion de la camara
XMFLOAT4 cameraTarget; // un vector que describe el punto en el espacio al que nuestra camara esta mirando 
XMFLOAT4 cameraUp; // vector worlds up

XMFLOAT4X4 cube1WorldMat; // la world matrix para el cubo 1 (transformation matrix)
XMFLOAT4X4 cube1RotMat; // esto hara un seguimiento de la rotacion para el cubo 1
XMFLOAT4 cube1Position; // posicion del cubo 1 en el espacio 

XMFLOAT4X4 cube2WorldMat; // la world matrix para el cubo 2 (transformation matrix)
XMFLOAT4X4 cube2RotMat; // esto hara un seguimiento de la rotacion para el cubo 2
XMFLOAT4 cube2PositionOffset; // el cubo 2 girara alrededor del cubo 1, esta es la position offset al cubo1
int numCubeIndices; // numero de indices para dibujar el cubo
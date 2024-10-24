#include <cstdio>

#include "Geometry.hpp"
#include "D3D11Renderer.hpp"

Geometry::Geometry(uint32_t vertexCount, uint32_t indexCount)
    : m_vertexCount(vertexCount)
    , m_indexCount(indexCount)
{
}

Geometry::~Geometry() {}

D3D11Geometry::D3D11Geometry(D3D11Renderer* renderer, uint32_t vertexCount, uint32_t indexCount)
    : Geometry(vertexCount, indexCount)
    , m_vertexBuffer(nullptr)
    , m_indexBuffer(nullptr)
    , m_renderer(renderer)
{
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = getVertexDataSize();
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

    HRESULT result = renderer->dxDevice()->CreateBuffer(&desc, nullptr, &m_vertexBuffer);
    if (result != S_OK) {
        printf("Failed to create vertex buffer: %d", GetLastError());
        abort();
    }

    desc.ByteWidth = getIndexDataSize();
    desc.BindFlags = D3D11_BIND_INDEX_BUFFER;

    result = renderer->dxDevice()->CreateBuffer(&desc, nullptr, &m_indexBuffer);
    if (result != S_OK) {
        printf("Failed to create index buffer: %d", GetLastError());
        abort();
    }
}

D3D11Geometry::~D3D11Geometry()
{
    m_vertexBuffer->Release();
    m_vertexBuffer = nullptr;

    m_indexBuffer->Release();
    m_indexBuffer = nullptr;
}

void D3D11Geometry::updateVertexBuffer(void* data) { m_renderer->dxDeviceContext()->UpdateSubresource(m_vertexBuffer, 0, nullptr, data, 0, 0); }
void D3D11Geometry::updateIndexBuffer(void* data) { m_renderer->dxDeviceContext()->UpdateSubresource(m_indexBuffer, 0, nullptr, data, 0, 0); }

GLGeometry::GLGeometry(uint32_t vertexCount, uint32_t indexCount)
    : Geometry(vertexCount, indexCount)
    , m_vao(0)
    , m_vertexBuffer(0)
    , m_indexBuffer(0)
{
    glGenBuffers(1, &m_vertexBuffer);
    glGenBuffers(1, &m_indexBuffer);

    uint32_t vertexDataSize = getVertexDataSize();
    uint32_t indexDataSize = getIndexDataSize();

    glBindBuffer(GL_ARRAY_BUFFER, m_vertexBuffer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_indexBuffer);

    glBufferStorage(GL_ARRAY_BUFFER, vertexDataSize, nullptr, 0);
    glBufferStorage(GL_ELEMENT_ARRAY_BUFFER, indexDataSize, nullptr, 0);

    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        printf("Failed to create geometry: %x", error);
        abort();
    }

    glGenVertexArrays(1, &m_vao);
    glBindVertexArray(m_vao);

    glBindBuffer(GL_ARRAY_BUFFER, m_vertexBuffer);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, false, sizeof(Vertex), 0);

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, false, sizeof(Vertex), reinterpret_cast<void*>(static_cast<uintptr_t>(sizeof(float) * 3)));

    glBindVertexArray(0);
}

GLGeometry::~GLGeometry()
{
    GLuint buffers[2] = {m_vertexBuffer, m_indexBuffer};
    glDeleteBuffers(2, buffers);

    m_vertexBuffer = 0;
    m_indexBuffer = 0;
}

void GLGeometry::updateVertexBuffer(void* data) { copyToBuffer(m_vertexBuffer, data, getVertexDataSize()); }
void GLGeometry::updateIndexBuffer(void* data) { copyToBuffer(m_indexBuffer, data, getIndexDataSize()); }

void GLGeometry::copyToBuffer(GLuint buffer, void* data, int32_t size)
{
    GLuint stagingBuffer;
    glGenBuffers(1, &stagingBuffer);

    glBindBuffer(GL_COPY_READ_BUFFER, stagingBuffer);
    glBufferStorage(GL_COPY_READ_BUFFER, getVertexDataSize(), data, GL_DYNAMIC_STORAGE_BIT);

    glCopyNamedBufferSubData(stagingBuffer, buffer, 0, 0, size);

    glDeleteBuffers(1, &stagingBuffer);

    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        printf("Failed to copy to buffer: %x", error);
        abort();
    }
}

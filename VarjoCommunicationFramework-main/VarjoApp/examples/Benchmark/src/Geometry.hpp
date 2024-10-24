#pragma once

#include <cstdint>
#include <GL/glew.h>
#include <d3d11.h>
#include <glm/vec3.hpp>

class D3D11Renderer;

/**
 * Geometry that has positions and normals.
 */
class Geometry
{
public:
    struct Vertex {
        glm::vec3 position;
        glm::vec3 normal;
    };

    Geometry(uint32_t vertexCount, uint32_t indexCount);
    virtual ~Geometry();

    virtual void updateVertexBuffer(void* data) = 0;
    virtual void updateIndexBuffer(void* data) = 0;

    uint32_t getVertexDataSize() const { return m_vertexCount * sizeof(Vertex); }
    uint32_t getIndexDataSize() const { return m_indexCount * sizeof(uint32_t); }

    uint32_t indexCount() const { return m_indexCount; }

protected:
    uint32_t m_vertexCount;
    uint32_t m_indexCount;
};

class D3D11Geometry : public Geometry
{
public:
    D3D11Geometry(D3D11Renderer* renderer, uint32_t vertexCount, uint32_t indexCount);
    ~D3D11Geometry();

    void updateVertexBuffer(void* data) override;
    void updateIndexBuffer(void* data) override;

    ID3D11Buffer* vertexBuffer() const { return m_vertexBuffer; }
    ID3D11Buffer* indexBuffer() const { return m_indexBuffer; }

private:
    ID3D11Buffer* m_vertexBuffer;
    ID3D11Buffer* m_indexBuffer;
    D3D11Renderer* m_renderer;
};

class GLGeometry : public Geometry
{
public:
    GLGeometry(uint32_t vertexCount, uint32_t indexCount);
    ~GLGeometry();

    void updateVertexBuffer(void* data) override;
    void updateIndexBuffer(void* data) override;

    GLuint vao() const { return m_vao; }
    GLuint indexBuffer() const { return m_indexBuffer; }

private:
    void copyToBuffer(GLuint buffer, void* data, int32_t size);

    GLuint m_vao;
    GLuint m_vertexBuffer;
    GLuint m_indexBuffer;
};

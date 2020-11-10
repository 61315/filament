/*
 * Copyright (C) 2020 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <filament/Camera.h>
#include <filament/ColorGrading.h>
#include <filament/Engine.h>
#include <filament/IndexBuffer.h>
#include <filament/LightManager.h>
#include <filament/Material.h>
#include <filament/RenderableManager.h>
#include <filament/Renderer.h>
#include <filament/RenderTarget.h>
#include <filament/Scene.h>
#include <filament/Skybox.h>
#include <filament/TextureSampler.h>
#include <filament/TransformManager.h>
#include <filament/VertexBuffer.h>
#include <filament/View.h>

#include <utils/EntityManager.h>

#include <filameshio/MeshReader.h>

#include <filamentapp/Config.h>
#include <filamentapp/FilamentApp.h>

#include "generated/resources/resources.h"
#include "generated/resources/monkey.h"

using namespace filament;
using namespace filamesh;
using namespace filament::math;

struct Vertex {
    float3 position;
    float2 uv;
};

struct App {
    utils::Entity lightEntity;
    Material* meshMaterial;
    MaterialInstance* meshMatInstance;
    MeshReader::Mesh mesh;
    mat4f transform;

    Texture* offscreenTexture = nullptr;
    RenderTarget* offscreenRenderTarget = nullptr;
    View* offscreenView = nullptr;
    Camera* offscreenCamera = nullptr;

    utils::Entity quadEntity;
    VertexBuffer* quadVb = nullptr;
    IndexBuffer* quadIb = nullptr;
    Material* quadMaterial = nullptr;
    MaterialInstance* quadMatInstance = nullptr;

    float3 quadCenter;
    float3 quadNormal;
    float3 quadExtents[2];
};

int main(int argc, char** argv) {
    Config config;
    config.title = "rendertarget";

    App app;
    auto setup = [config, &app](Engine* engine, View* view, Scene* scene) {
        scene->setSkybox(nullptr);

        ColorGrading* color_grading = ColorGrading::Builder()
                .toneMapping(ColorGrading::ToneMapping::FILMIC)
                .build(*engine);

        view->setColorGrading(color_grading);
        view->setVignetteOptions({ .enabled = true });

        auto& tcm = engine->getTransformManager();
        auto& rcm = engine->getRenderableManager();
        auto& em = utils::EntityManager::get();

        utils::Entity cameraEntity = em.create();
        app.offscreenCamera = engine->createCamera(cameraEntity);

        // Instantiate offscreen render target
        app.offscreenView = engine->createView();
        app.offscreenView->setScene(scene);
        app.offscreenView->setColorGrading(color_grading);
        app.offscreenTexture = Texture::Builder()
            .width(1024).height(1024).levels(1)
            .usage(Texture::Usage::COLOR_ATTACHMENT | Texture::Usage::SAMPLEABLE)
            .format(Texture::InternalFormat::RGBA8).build(*engine);
        app.offscreenRenderTarget = RenderTarget::Builder()
            .texture(RenderTarget::COLOR, app.offscreenTexture)
            .build(*engine);
        app.offscreenView->setRenderTarget(app.offscreenRenderTarget);
        app.offscreenView->setViewport({0, 0, 1024, 1024});
        app.offscreenView->setCamera(app.offscreenCamera);
        FilamentApp::get().addOffscreenView(app.offscreenView);

        // Position the quad as desired.
        float3 c = app.quadCenter = {-2, 0, -5};
        float3 n = app.quadNormal = normalize(float3 {1, 0, 2});
        float3 u = normalize(cross(app.quadNormal, float3(0, 1, 0)));
        float3 v = cross(n, u);
        u = app.quadExtents[0] = 1.5 * u;
        v = app.quadExtents[1] = 1.5 * v;
        static Vertex kQuadVertices[4] = { {{}, {0, 0}}, {{}, {1, 0}}, {{}, {0, 1}}, {{}, {1, 1}} };
        kQuadVertices[0].position = c - u - v;
        kQuadVertices[1].position = c + u - v;
        kQuadVertices[2].position = c - u + v;
        kQuadVertices[3].position = c + u + v;

        // Create quad vertex buffer.
        static_assert(sizeof(Vertex) == 20, "Strange vertex size.");
        app.quadVb = VertexBuffer::Builder()
                .vertexCount(4)
                .bufferCount(1)
                .attribute(VertexAttribute::POSITION, 0, VertexBuffer::AttributeType::FLOAT3, 0, 20)
                .attribute(VertexAttribute::UV0, 0, VertexBuffer::AttributeType::FLOAT2, 12, 20)
                .build(*engine);
        app.quadVb->setBufferAt(*engine, 0,
                VertexBuffer::BufferDescriptor(kQuadVertices, 80, nullptr));

        // Create quad index buffer.
        static constexpr uint16_t kQuadIndices[6] = { 0, 1, 2, 3, 2, 1 };
        app.quadIb = IndexBuffer::Builder()
                .indexCount(6)
                .bufferType(IndexBuffer::IndexType::USHORT)
                .build(*engine);
        app.quadIb->setBuffer(*engine, IndexBuffer::BufferDescriptor(kQuadIndices, 12, nullptr));

        // Create quad material and renderable.
        app.quadMaterial = Material::Builder()
                .package(RESOURCES_BAKEDTEXTURE_DATA, RESOURCES_BAKEDTEXTURE_SIZE)
                .build(*engine);
        app.quadMatInstance = app.quadMaterial->createInstance();
        TextureSampler sampler(TextureSampler::MinFilter::LINEAR, TextureSampler::MagFilter::LINEAR);
        app.quadMatInstance->setParameter("albedo", app.offscreenTexture, sampler);
        app.quadEntity = em.create();
        RenderableManager::Builder(1)
                .boundingBox({{ -1, -1, -1 }, { 1, 1, 1 }})
                .material(0, app.quadMatInstance)
                .geometry(0, RenderableManager::PrimitiveType::TRIANGLES, app.quadVb, app.quadIb, 0, 6)
                .culling(false)
                .receiveShadows(false)
                .castShadows(false)
                .build(*engine, app.quadEntity);
        scene->addEntity(app.quadEntity);

        // Instantiate mesh material.
        app.meshMaterial = Material::Builder()
            .package(RESOURCES_AIDEFAULTMAT_DATA, RESOURCES_AIDEFAULTMAT_SIZE).build(*engine);
        auto mi = app.meshMatInstance = app.meshMaterial->createInstance();
        mi->setParameter("baseColor", RgbType::LINEAR, {0.8, 1.0, 1.0});
        mi->setParameter("metallic", 0.0f);
        mi->setParameter("roughness", 0.4f);
        mi->setParameter("reflectance", 0.5f);

        // Add geometry into the scene.
        app.mesh = MeshReader::loadMeshFromBuffer(engine, MONKEY_SUZANNE_DATA, nullptr, nullptr, mi);
        auto ti = tcm.getInstance(app.mesh.renderable);
        app.transform = mat4f{ mat3f(1), float3(0, 0, -4) } * tcm.getWorldTransform(ti);
        rcm.setCastShadows(rcm.getInstance(app.mesh.renderable), false);
        scene->addEntity(app.mesh.renderable);

        // Add light sources into the scene.
        app.lightEntity = em.create();
        LightManager::Builder(LightManager::Type::SUN)
                .color(Color::toLinear<ACCURATE>(sRGBColor(0.98f, 0.92f, 0.89f)))
                .intensity(110000)
                .direction({ 0.7, -1, -0.8 })
                .sunAngularRadius(1.9f)
                .castShadows(false)
                .build(*engine, app.lightEntity);
        scene->addEntity(app.lightEntity);
    };

    auto cleanup = [&app](Engine* engine, View*, Scene*) {
        engine->destroy(app.lightEntity);
        engine->destroy(app.quadEntity);
        engine->destroy(app.meshMatInstance);
        engine->destroy(app.meshMaterial);
        engine->destroy(app.mesh.renderable);
        engine->destroy(app.mesh.vertexBuffer);
        engine->destroy(app.mesh.indexBuffer);
        engine->destroy(app.offscreenTexture);
        engine->destroy(app.offscreenRenderTarget);
        engine->destroy(app.offscreenView);
        engine->destroy(app.quadVb);
        engine->destroy(app.quadIb);
        engine->destroy(app.quadMatInstance);
        engine->destroy(app.quadMaterial);
    };

    auto preRender = [&app](Engine*, View*, Scene*, Renderer* renderer) {
        renderer->setClearOptions({.clearColor = {0.1,0.2,0.4,1.0}, .clear = true});
    };

    FilamentApp::get().animate([&app](Engine* engine, View* view, double now) {
        auto& tcm = engine->getTransformManager();
        Camera& mainCamera = view->getCamera();

        // First, rotate the monkey and slide her along Z.
        auto ti = tcm.getInstance(app.mesh.renderable);
        mat4f xlate = mat4f::translation(float3(0, 0, 0.5 + sin(now)));
        tcm.setTransform(ti, app.transform * xlate * mat4f::rotation(now, float3{ 0, 1, 0 }));

        // Mimic the GLSL reflection function, which returns R in the following diagram.
        // The I vector is pointing down, the R vector is pointed up.
        //
        //    I     N     R
        //     \    ^    /
        //      \   |   /
        //       \  |  /
        //        \ | /
        // =================
        //
        auto reflect = [](float3 I, float3 N) {
            return I - 2.0 * dot(N, I) * N;
        };

        // Given an arbitrary point on a plane and its normal, return the Ray-Plane intersection.
        auto intersectPlane = [](float3 planePt, float3 planeNormal, float3 rayOrigin, float3 rayDir) {
            const float t = dot(planePt - rayOrigin, planeNormal) / dot(rayDir, planeNormal);
            return rayOrigin + t * rayDir;
        };

        // Formulate the offscreen camera by reflecting the main camera.
        const float3 eyePos = mainCamera.getPosition();
        const float3 upVec = mainCamera.getUpVector();
        const float3 gazeVec = mainCamera.getForwardVector();
        const float3 quadPoint = intersectPlane(app.quadCenter, app.quadNormal, eyePos, gazeVec);
        const float3 reflectedGazeVec = reflect(gazeVec, app.quadNormal);
        const float3 reflectedEyePos = quadPoint - distance(quadPoint, eyePos) * reflectedGazeVec;
        const float3 reflectedUpVec = reflect(upVec, app.quadNormal);
        app.offscreenCamera->lookAt(reflectedEyePos, reflectedEyePos + reflectedGazeVec, reflectedUpVec);

        const double aspectRatio = 1.0;
        const float focalLength = FilamentApp::get().getCameraFocalLength();
        app.offscreenCamera->setLensProjection(focalLength, aspectRatio, 0.1, 100.0);
    });

    FilamentApp::get().run(config, setup, cleanup, FilamentApp::ImGuiCallback(), preRender);

    return 0;
}

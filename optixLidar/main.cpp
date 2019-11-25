#include "SampleRenderer.h"
#include "LidarRenderer.h"
#include "Lidar.h"


// our helper library for window handling
#include "glfWindow/GLFWindow.h"
#include <GL/gl.h>

#include <cmath>
#include <fstream>

std::string pointsFileName = "points.xyz";

void savePointsToFile(std::vector<float> &points);

#if defined(_WIN32)
#include <chrono>
#include <time.h>

struct timeval {
	long tv_sec;
	long tv_usec;
};


int gettimeofday(struct timeval* tp, struct timezone* tzp) {
	namespace sc = std::chrono;
	sc::system_clock::duration d = sc::system_clock::now().time_since_epoch();
	sc::seconds s = sc::duration_cast<sc::seconds>(d);
	tp->tv_sec = s.count();
	tp->tv_usec = sc::duration_cast<sc::microseconds>(d - s).count();

	return 0;
}

#endif // _WIN32

long long current_timestamp()
{
    struct timeval te;
    gettimeofday(&te, NULL); // get current time
    long long milliseconds = te.tv_sec*1000LL + te.tv_usec/1000; // calculate milliseconds
    return milliseconds;
}


struct SampleWindow : public GLFCameraWindow
{
    SampleWindow(const std::string &title,
                 Model *model,
                 std::vector<Model *>models,
                 const Camera &camera,
                 const float worldScale,
                 vec3f lidarInitialSource,
                 vec3f lidarInitialDirection,
                 float lidarInitialWidth,
                 float lidarInitialHeight,
                 int samplingInitialWidth,
                 int samplingInitialHeight,
                 float range)
        : GLFCameraWindow(title, camera.from, camera.at, camera.up, worldScale), model(model),
          lidarRend(model, range), sample(model), lidar(lidarInitialSource, lidarInitialDirection, lidarInitialWidth,
          lidarInitialHeight, samplingInitialWidth, samplingInitialHeight, range), models(models)
    {
        sample.setCamera(camera);
    }

    virtual void render() override
    {
        // for calculating frame rendering time
        static long long begin = current_timestamp();

        if (cameraFrame.modified) {
            sample.setCamera(Camera{cameraFrame.get_from(),
                                    cameraFrame.get_at(),
                                    cameraFrame.get_up() });
            cameraFrame.modified = false;
        }
        
        
        
        static int counter = 0;
        static int moveCounter = 0;
//        if (counter%10 == 0 )
        {
            Model * currentModel = new Model;
            // read tunnel
            for (int i = 0; i < model->meshes.size(); ++i)
            {
                currentModel->meshes.push_back(model->meshes[i]);
            }
            for (int i = 0; i < model->textures.size(); ++i)
            {
                currentModel->textures.push_back(model->textures[i]);
            }
            // add human
            for (int i = 0; i < models[moveCounter]->meshes.size(); ++i)
            {
                currentModel->meshes.push_back(models[moveCounter]->meshes[i]);
            }

            for (auto mesh : currentModel->meshes)
                for (auto vtx : mesh->vertex)
                    currentModel->bounds.extend(vtx);
            
            model->moved = false;
            currentModel->moved = true;
            
            // set model in renderers
            sample.setModel(currentModel);
            lidarRend.setModel(currentModel);
            
            moveCounter++;
            if (moveCounter == 38)
                moveCounter = 0;
        }
        counter++;


        printf("%lld\n", current_timestamp()-begin);
        begin = current_timestamp();
        std::vector<float> points;
        lidarRend.resize(lidar.rays.size()/6);

//        printf("         %lld\n", current_timestamp()-begin);
//        begin = current_timestamp();
        lidarRend.render(lidar.rays);

//        printf("%lld\n", current_timestamp()-begin);
//        begin = current_timestamp();
 
        lidarRend.downloadPoints(points);

//        printf("download %lld\n", current_timestamp()-begin);
//        begin = current_timestamp();

//printf("rays: %d, points: %d\n", lidar.rays.size(), points.size());
//        savePointsToFile(points);
//        printf("save     %lld\n", current_timestamp()-begin);
//        begin = current_timestamp();

        sample.resizeLidar(points.size()/4);
//        printf("         %lld\n", current_timestamp()-begin);
//        begin = current_timestamp();

        sample.render(points);

//       printf("render 2 %lld\n\n", current_timestamp()-begin);
//       printf("%lld\n", current_timestamp()-begin);
//       begin = current_timestamp();
        
    }

    virtual void draw() override
    {
        sample.downloadPixels(pixels.data());
        if (fbTexture == 0)
            glGenTextures(1, &fbTexture);

        glBindTexture(GL_TEXTURE_2D, fbTexture);
        GLenum texFormat = GL_RGBA;
        GLenum texelType = GL_UNSIGNED_BYTE;
        glTexImage2D(GL_TEXTURE_2D, 0, texFormat, fbSize.x, fbSize.y, 0, GL_RGBA,
                     texelType, pixels.data());

        glDisable(GL_LIGHTING);
        glColor3f(1, 1, 1);

        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();

        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, fbTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        glDisable(GL_DEPTH_TEST);

        glViewport(0, 0, fbSize.x, fbSize.y);

        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0.f, (float)fbSize.x, 0.f, (float)fbSize.y, -1.f, 1.f);

        glBegin(GL_QUADS);
        {
            glTexCoord2f(0.f, 0.f);
            glVertex3f(0.f, 0.f, 0.f);

            glTexCoord2f(0.f, 1.f);
            glVertex3f(0.f, (float)fbSize.y, 0.f);

            glTexCoord2f(1.f, 1.f);
            glVertex3f((float)fbSize.x, (float)fbSize.y, 0.f);

            glTexCoord2f(1.f, 0.f);
            glVertex3f((float)fbSize.x, 0.f, 0.f);
        }
        glEnd();
    }

    virtual void resize(const vec2i &newSize)
    {
        fbSize = newSize;
        sample.resize(newSize);
        pixels.resize(newSize.x*newSize.y);
    }

    virtual void key(int key, int mods)
    {
//        printf("%d %d\n", key, mods);
        switch(key)
        {
        case 265: // forward
            lidar.moveX(10.f);
            break;
        case 264: // back
            lidar.moveX(-10.f);
            break;
        case 262: // right
            lidar.moveZ(10.f);
            break;
        case 263: // left
            lidar.moveZ(-10.f);
            break;
        case 65: // a
            lidar.rotateY(-0.1f);
            break;
        case 68: // d
            lidar.rotateY(0.1f);
            break;
        case 83: // s
            lidar.rotateZ(-0.1f);
            break;
        case 87: // w
            lidar.rotateZ(0.1f);
            break;
        }

    }


    vec2i                 fbSize;
    GLuint                fbTexture {0};
    Model *               model;
    LidarRenderer         lidarRend;
    SampleRenderer        sample;
    Lidar                 lidar;
    std::vector<uint32_t> pixels;
    std::vector<Model *>  models;
};


/*! main entry point to this example - initially optix, print hello
world, then exit */
extern "C" int main(int ac, char **av)
{
    try {
        Model *modelStatic = loadOBJ("../models/tunnel.obj");
        
        std::vector<Model *> models;
        for (int i = 1; i <= 38; ++i)
        {
            std::string modelName = "../models/optixTestNoMaterial/DAZ_Worker_tmp_0000";
            if (i < 10)
                modelName += "0";
            modelName += std::to_string(i);
            modelName += ".obj";
            Model *model = loadOBJ(modelName.c_str());
            for (int i = 0; i < model->meshes.size(); ++i)
            {
                for (int j = 0; j < model->meshes[i]->vertex.size(); ++j)
                {
                    model->meshes[i]->vertex[j].x *= 100;
                    model->meshes[i]->vertex[j].x -= 3000;
                    model->meshes[i]->vertex[j].y *= 100;
                    model->meshes[i]->vertex[j].y += 425;
                    model->meshes[i]->vertex[j].z *= 100;
                    model->meshes[i]->vertex[j].z -= 100;
                }
            }
            models.push_back(model);
        }
        
//        Camera camera = { /*from*/vec3f(-20.f, 0.f, 0.f),
        Camera camera = { /*from*/vec3f(-4000.07f, 450.f, 0.f),
//                          /* at */model->bounds.center()-vec3f(0,400,0),
                          /* at */vec3f(1,0.06,0),
                          /* up */vec3f(0.f,1.f,0.f) };
        // something approximating the scale of the world, so the
        // camera knows how much to move for any given user interaction:
        const float worldScale = length(models[0]->bounds.span());

        vec3f lidarInitialSource = vec3f(-4000.f, 450.f, 0.f);
        vec3f lidarInitialDirection = vec3f(1.f, 0.f, 0.f);

        
        // this values we need to compare
        float lidarInitialWidth = 240*M_PI/180.f; // angle in radians
        float lidarInitialHeight = 30*M_PI/180.f; // angle in radians
        int samplingInitialWidth = 30;//1149;
        int samplingInitialHeight = 10;//240;
		
        float range = 2000.f; // 40m * 50

        SampleWindow *window = new SampleWindow("Optix lidar",
                                                modelStatic, models, camera, worldScale,
                                                lidarInitialSource, lidarInitialDirection,
                                                lidarInitialWidth, lidarInitialHeight, samplingInitialWidth,
                                                samplingInitialHeight, range);
        window->run();

    } catch (std::runtime_error& e) {
      std::cout << GDT_TERMINAL_RED << "FATAL ERROR: " << e.what()
                << GDT_TERMINAL_DEFAULT << std::endl;
      exit(1);
    }
    return 0;
}

void savePointsToFile(std::vector<float> &points)
{

    std::ofstream file(pointsFileName);
    file << points.size()/4 << "\n\n";

    for (int i = 0; i < points.size()/4; ++i)
    {
        file << points[4*i+0] << " " << points[4*i+1] << " " << points[4*i+2] << " ";

        const int intensity = int(255.99f*points[4*i+3]);
        file << intensity << '\n';
    }
    file.close();

}

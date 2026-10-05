#include "Docker/Window.hpp"
#include "Docker/Template/WindowTemplate.hpp"
#include "Help Menu/About.hpp"
#include "Tempestus Aquila/Aquila-Panel.hpp"

#include <string>

static void onViewport(int x, int y, int w, int h)
{
    glViewport(x, y, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluOrtho2D(0, w, 0, h);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

static void display()
{
    int x, y, w, h;
    GLUI_Master.get_viewport_area(&x, &y, &w, &h);

    glClearColor(0.12f, 0.12f, 0.14f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // Placeholder scene area
    glColor3f(0.2f, 0.35f, 0.6f);
    glBegin(GL_QUADS);
    glVertex2i(20, 20);
    glVertex2i(w - 20, 20);
    glVertex2i(w - 20, h - 20);
    glVertex2i(20, h - 20);
    glEnd();

    const std::string& status = Docker::instance().status();
    glColor3f(1, 1, 1);
    glRasterPos2i(30, 30);
    for (size_t i = 0; i < status.size(); ++i)
        glutBitmapCharacter(GLUT_BITMAP_HELVETICA_12, status[i]);

    glutSwapBuffers();
}

int main(int argc, char** argv)
{
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_RGB | GLUT_DOUBLE);
    glutInitWindowSize(900, 600);
    const std::string windowTitle = applicationWindowTitle();
    int mainWindow = glutCreateWindow(windowTitle.c_str());

    glutDisplayFunc(display);

    Docker& docker = Docker::instance();
    docker.init(mainWindow, onViewport);

    int aquilaPanel = docker.addPanel("Aquila", buildAquilaPanel, true,
        DockerTemplate::addWindowControls);
    int aboutPanel = docker.addPanel("About", buildAboutPanel, false);

    int toolsMenu = docker.addMenu("Tools");
    docker.addMenuItem(toolsMenu, "TA", [&docker, aquilaPanel]() {
        docker.openPanel(aquilaPanel, Docker::FLOATING);
    });

    int helpMenu = docker.addMenu("Help");
    docker.addMenuItem(helpMenu, "About", [&docker, aboutPanel]() {
        docker.openPanel(aboutPanel, Docker::FLOATING);
    });

    glutMainLoop();
    return 0;
}

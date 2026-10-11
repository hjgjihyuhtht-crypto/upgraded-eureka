// Kinect 3D Studio: no PC espera o Kinect e oferece "Ver sensores" ou "Jogar".
// No Android este main() é chamado pelo raylib (android_main) dentro da GameActivity.
#include <memory>

#include "app/app.h"

int main(int argc, char **argv) {
    auto app = std::make_unique<k3d::App>();   // no heap: a pilha da thread nativa do Android é pequena
    return app->run(argc, argv);
}

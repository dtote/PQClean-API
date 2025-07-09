FROM ubuntu:20.04

# Configurar zona horaria de forma no interactiva
ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=Europe/Madrid

RUN apt-get update && apt-get install -y \
    g++ \
    cmake \
    libssl-dev \
    git \
    build-essential \
    libboost-all-dev \
    wget \
    libasio-dev \
    pkg-config

WORKDIR /app

# Instalar Crow v1.0+3 (la más estable hasta la fecha)
RUN git clone https://github.com/CrowCpp/Crow.git && \
    cd Crow && \
    git checkout v1.0+3 && \
    mkdir build && \
    cd build && \
    cmake .. \
        -DCROW_BUILD_EXAMPLES=OFF \
        -DCROW_BUILD_TESTS=OFF \
        -DCROW_BUILD_DOCS=OFF \
        -DCROW_ENABLE_SSL=OFF && \
    make -j$(nproc) && \
    make install && \
    ldconfig && \
    cd /app && \
    rm -rf Crow

# Instalar base64 (copiamos el .cpp al directorio de trabajo)
RUN git clone https://github.com/ReneNyffenegger/cpp-base64.git && \
    cp cpp-base64/base64.h /usr/local/include/ && \
    cp cpp-base64/base64.cpp ./base64.cpp && \
    rm -rf cpp-base64

# Clonar PQClean
RUN git clone --depth 1 https://github.com/PQClean/PQClean.git

# Compilar los algoritmos específicos y sus dependencias
RUN cd PQClean && \
    # Primero explorar la estructura para encontrar los archivos
    echo "=== Estructura del directorio common ===" && \
    ls -la common/ && \
    echo "=== Buscando archivos fips202.c y randombytes.c ===" && \
    find . -name "fips202.c" && \
    find . -name "randombytes.c" && \
    echo "=== Compilando con las rutas encontradas ===" && \
    # Compilar las funciones criptográficas usando rutas relativas desde PQClean/
    gcc -c -fPIC common/fips202.c -o fips202.o && \
    gcc -c -fPIC common/randombytes.c -o randombytes.o && \
    ar rcs libpqclean_common.a fips202.o randombytes.o && \
    # Compilar los algoritmos
    make -C crypto_kem/ml-kem-512/clean && \
    make -C crypto_kem/ml-kem-768/clean && \
    make -C crypto_kem/ml-kem-1024/clean && \
    make -C crypto_sign/ml-dsa-44/clean && \
    make -C crypto_sign/ml-dsa-65/clean && \
    make -C crypto_sign/ml-dsa-87/clean && \
    # Verificar dónde están las bibliotecas
    find . -name "*.a" > /tmp/libraries.txt && \
    mkdir -p build/lib && \
    # Copiar todas las bibliotecas a un directorio común
    for lib in $(cat /tmp/libraries.txt); do \
        cp $lib build/lib/; \
    done && \
    # Copiar también la biblioteca común que acabamos de crear
    cp libpqclean_common.a build/lib/

# Copiar el archivo fuente al contenedor
COPY pqclean-api.cpp .

# Compilar la API con soporte para C++17, incluyendo las dependencias criptográficas
RUN g++ -std=c++17 -o pqclean-api pqclean-api.cpp base64.cpp -I. -I./PQClean \
    -L./PQClean/build/lib \
    -lml-kem-512_clean -lml-kem-768_clean -lml-kem-1024_clean \
    -lml-dsa-44_clean -lml-dsa-65_clean -lml-dsa-87_clean \
    -lpqclean_common \
    -lpthread \
    `pkg-config --cflags --libs crow`
    
# Exponer el puerto que usa la aplicación
EXPOSE 5003

CMD ["./pqclean-api"]
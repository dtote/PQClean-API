#include "crow.h"
#include <string>
#include <mutex>
#include <iostream>
#include <cstring>
#include <base64.h>
#include <vector>

// Incluir los headers de PQClean
extern "C"
{
#include "crypto_kem/ml-kem-512/clean/api.h"
#include "crypto_sign/ml-dsa-44/clean/api.h"
}

// Función para generar claves ML-KEM (Kyber)
std::pair<std::string, std::string> generate_kem_keys()
{
  // Asignar memoria para las claves
  unsigned char pk[PQCLEAN_MLKEM512_CLEAN_CRYPTO_PUBLICKEYBYTES];
  unsigned char sk[PQCLEAN_MLKEM512_CLEAN_CRYPTO_SECRETKEYBYTES];

  // Generar par de claves
  PQCLEAN_MLKEM512_CLEAN_crypto_kem_keypair(pk, sk);

  // Codificar en base64
  std::string public_key_base64 = base64_encode(pk, PQCLEAN_MLKEM512_CLEAN_CRYPTO_PUBLICKEYBYTES);
  std::string secret_key_base64 = base64_encode(sk, PQCLEAN_MLKEM512_CLEAN_CRYPTO_SECRETKEYBYTES);

  return {public_key_base64, secret_key_base64};
}

// Función para generar claves ML-DSA (Dilithium)
std::pair<std::string, std::string> generate_dsa_keys()
{
  // Asignar memoria para las claves
  unsigned char pk[PQCLEAN_MLDSA44_CLEAN_CRYPTO_PUBLICKEYBYTES];
  unsigned char sk[PQCLEAN_MLDSA44_CLEAN_CRYPTO_SECRETKEYBYTES];

  // Generar par de claves
  PQCLEAN_MLDSA44_CLEAN_crypto_sign_keypair(pk, sk);

  // Codificar en base64
  std::string public_key_base64 = base64_encode(pk, PQCLEAN_MLDSA44_CLEAN_CRYPTO_PUBLICKEYBYTES);
  std::string secret_key_base64 = base64_encode(sk, PQCLEAN_MLDSA44_CLEAN_CRYPTO_SECRETKEYBYTES);

  return {public_key_base64, secret_key_base64};
}

// Función para encriptar un mensaje
std::pair<std::string, std::string> encrypt_message(const std::string &message, const std::string &public_key_base64)
{
  // Decodificar clave pública
  std::string public_key_bytes = base64_decode(public_key_base64);
  unsigned char *pk = (unsigned char *)public_key_bytes.data();

  // Generar ciphertext y clave compartida
  unsigned char ct[PQCLEAN_MLKEM512_CLEAN_CRYPTO_CIPHERTEXTBYTES];
  unsigned char ss[PQCLEAN_MLKEM512_CLEAN_CRYPTO_BYTES];

  // Encapsular
  if (PQCLEAN_MLKEM512_CLEAN_crypto_kem_enc(ct, ss, (unsigned char *)public_key_bytes.data()) != 0)
  {
    throw std::runtime_error("Failed to encrypt message");
  }

  std::vector<std::string> encrypted_blocks;
  size_t block_size = 32;

  for (size_t i = 0; i < message.size(); i += block_size)
  {
    std::string block = message.substr(i, block_size);
    std::vector<unsigned char> xor_encrypted_block(block.size());

    // XOR con la clave compartida
    for (size_t j = 0; j < block.size(); j++)
    {
      xor_encrypted_block[j] = block[j] ^ ss[j % PQCLEAN_MLKEM512_CLEAN_CRYPTO_BYTES];
    }

    std::string encoded = base64_encode(xor_encrypted_block.data(), block.size());
    encrypted_blocks.push_back(encoded);
  }

  // Concatenar bloques con delimitador "::"
  std::string xor_encrypted_base64 = "";
  for (size_t i = 0; i < encrypted_blocks.size(); i++)
  {
    xor_encrypted_base64 += encrypted_blocks[i];
    if (i < encrypted_blocks.size() - 1)
      xor_encrypted_base64 += "::";
  }

  std::string shared_secret_base64 = base64_encode(ss, PQCLEAN_MLKEM512_CLEAN_CRYPTO_BYTES);

  return {xor_encrypted_base64, shared_secret_base64};
}

// Función para descifrar (necesaria para completar el par)
std::string decrypt_message(const std::string &ciphertext_combined, const std::string &shared_secret_base64)
{
  // Decodificar clave compartida
  std::string shared_secret_str = base64_decode(shared_secret_base64);

  // Separar bloques por delimitador "::"
  std::vector<std::string> encrypted_blocks;
  size_t pos = 0, next;
  while ((next = ciphertext_combined.find("::", pos)) != std::string::npos)
  {
    encrypted_blocks.push_back(ciphertext_combined.substr(pos, next - pos));
    pos = next + 2;
  }
  encrypted_blocks.push_back(ciphertext_combined.substr(pos));

  std::string original_message = "";

  // Descifrar cada bloque
  for (const auto &encoded_block : encrypted_blocks)
  {
    std::string decoded = base64_decode(encoded_block);
    std::vector<unsigned char> decrypted_block(decoded.size());

    // XOR para descifrar
    for (size_t i = 0; i < decoded.size(); i++)
    {
      decrypted_block[i] = decoded[i] ^ shared_secret_str[i % shared_secret_str.size()];
    }

    original_message += std::string(reinterpret_cast<char *>(decrypted_block.data()), decoded.size());
  }

  return original_message;
}

// Función para firmar un mensaje
std::string sign_message(const std::string &message, const std::string &secret_key_base64)
{
  // Decodificar clave privada
  std::string secret_key_bytes = base64_decode(secret_key_base64);

  // Preparar mensaje y firma
  size_t message_len = message.length();
  unsigned char *m = (unsigned char *)message.data();
  std::vector<unsigned char> sm(message_len + PQCLEAN_MLDSA44_CLEAN_CRYPTO_BYTES);
  size_t smlen;

  // Firmar mensaje
  PQCLEAN_MLDSA44_CLEAN_crypto_sign(
      sm.data(), &smlen,
      m, message_len,
      (unsigned char *)secret_key_bytes.data());

  // Codificar firma en base64
  std::string signature_base64 = base64_encode(sm.data(), smlen - message_len);

  return signature_base64;
}

// Función para verificar una firma
bool verify_signature(const std::string &message, const std::string &signature_base64, const std::string &public_key_base64)
{
  // Decodificar clave pública y firma
  std::string public_key_bytes = base64_decode(public_key_base64);
  std::string signature_bytes = base64_decode(signature_base64);

  // Reconstruir el mensaje firmado (firma + mensaje original)
  std::vector<unsigned char> sm(signature_bytes.length() + message.length());
  std::memcpy(sm.data(), signature_bytes.data(), signature_bytes.length());
  std::memcpy(sm.data() + signature_bytes.length(), message.data(), message.length());

  // Buffer para el mensaje verificado
  std::vector<unsigned char> m(message.length());
  size_t mlen;

  // Verificar firma
  int result = PQCLEAN_MLDSA44_CLEAN_crypto_sign_open(
      m.data(), &mlen,
      sm.data(), sm.size(),
      (unsigned char *)public_key_bytes.data());

  return result == 0 && mlen == message.length();
}

int main()
{
  std::cout << "Starting PQClean API server..." << std::endl;
  crow::SimpleApp app;

  // Endpoint para generar claves ML-KEM
  app.route_dynamic("/generate_keys").methods(crow::HTTPMethod::POST)([&](const crow::request &req) -> crow::response
                                                                      {
            try {
                auto [public_key, secret_key] = generate_kem_keys();
                
                return crow::response(crow::json::wvalue({
                    {"public_key", public_key},
                    {"secret_key", secret_key}
                }));
            } catch (const std::exception &e) {
                return crow::response(500, e.what());
            } });

  // Endpoint para generar claves ML-DSA
  app.route_dynamic("/generate_ml_dsa_keys").methods(crow::HTTPMethod::POST)([&](const crow::request &req) -> crow::response
                                                                             {
            try {
                auto [public_key, private_key] = generate_dsa_keys();
                
                return crow::response(crow::json::wvalue({
                    {"public_key", public_key},
                    {"private_key", private_key}
                }));
            } catch (const std::exception &e) {
                return crow::response(500, e.what());
            } });

  // Endpoint para cifrar mensajes
  app.route_dynamic("/encrypt").methods(crow::HTTPMethod::POST)([&](const crow::request &req) -> crow::response
                                                                {
            try {
                auto json_data = crow::json::load(req.body);
                if (!json_data || !json_data.has("message") || !json_data.has("public_key")) {
                    return crow::response(400, "Invalid request");
                }

                std::string message = json_data["message"].s();
                std::string public_key = json_data["public_key"].s();

                auto [ciphertext, shared_secret] = encrypt_message(message, public_key);

                return crow::response(crow::json::wvalue({
                    {"ciphertext", ciphertext},
                    {"shared_secret", shared_secret}
                }));
            } catch (const std::exception &e) {
                return crow::response(500, e.what());
            } });

  // Endpoint para descifrar mensajes
  app.route_dynamic("/decrypt").methods(crow::HTTPMethod::POST)([&](const crow::request &req) -> crow::response
                                                                {
    try
    {
      auto json_data = crow::json::load(req.body);
      if (!json_data || !json_data.has("ciphertext") || !json_data.has("shared_secret"))
      {
        return crow::response(400, "Invalid request");
      }

      std::string ciphertext = json_data["ciphertext"].s();
      std::string shared_secret = json_data["shared_secret"].s();

      std::string plaintext = decrypt_message(ciphertext, shared_secret);

      return crow::response(crow::json::wvalue({{"original_message", plaintext}}));
    }
    catch (const std::exception &e)
    {
      return crow::response(500, e.what());
    } });

  // Endpoint para firmar mensajes
  app.route_dynamic("/sign").methods(crow::HTTPMethod::POST)([&](const crow::request &req) -> crow::response
                                                             {
    try
    {
      auto json_data = crow::json::load(req.body);
      if (!json_data || !json_data.has("message") || !json_data.has("private_key"))
      {
        return crow::response(400, "Invalid request");
      }

      std::string message = json_data["message"].s();
      std::string private_key = json_data["private_key"].s();

      std::string signature = sign_message(message, private_key);

      return crow::response(crow::json::wvalue({{"signature", signature}}));

    }
    catch (const std::exception &e)
    {
      return crow::response(500, e.what());
    } });

  // Endpoint para verificar firmas
  app.route_dynamic("/verify").methods(crow::HTTPMethod::POST)([&](const crow::request &req) -> crow::response
                                                               {
    try
    {
      auto json_data = crow::json::load(req.body);
      if (!json_data || !json_data.has("message") || !json_data.has("signature") || !json_data.has("public_key"))
      {
        return crow::response(400, "Invalid request");
      }

      std::string message = json_data["message"].s();
      std::string signature = json_data["signature"].s();
      std::string public_key = json_data["public_key"].s(); 

      bool is_valid = verify_signature(message, signature, public_key); 

      if (is_valid) {
        return crow::response(crow::json::wvalue({
          { "status", "verified" }
        }));
      } else {
        return crow::response(400, "Signature verification failed");
      }
    }
    catch (const std::exception &e)
    {
      return crow::response(500, e.what());
    } });

  // Endpoint para descifrado masivo
  app.route_dynamic("/bulkDecrypt").methods(crow::HTTPMethod::POST)([&](const crow::request &req) -> crow::response
                                                                    {
    try
    {
      auto json_data = crow::json::load(req.body);
      if (!json_data || !json_data.has("messages"))
      {
        return crow::response(400, "Invalid request");
      }
      
      auto messages_array = json_data["messages"];
      crow::json::wvalue response;
      response["results"] = crow::json::wvalue::list();
      
      int i = 0;
      for (auto &msg : messages_array)
      {
        try
        {
          std::string ciphertext = msg["ciphertext"].s();
          std::string shared_secret = msg["shared_secret"].s(); 
          
          std::string plaintext = decrypt_message(ciphertext, shared_secret); 

          crow::json::wvalue result;
          result["original_message"] = plaintext;
          response["results"][i] = std::move(result);
        }
        catch (const std::exception &e)
        {
          crow::json::wvalue error; 
          error["error"] = e.what();
          response["results"][i] = std::move(error);
        }
        i++;
      }
      return crow::response(200, response);
    }
    catch (const std::exception &e)
    {
      return crow::response(500, e.what());
    } });

  // Endpoint para verificación masiva
  app.route_dynamic("/bulkVerify").methods(crow::HTTPMethod::POST)([&](const crow::request &req) -> crow::response
                                                                   {
    try
    {
      auto json_data = crow::json::load(req.body);
      if (!json_data || !json_data.has("messages"))
      {
        return crow::response(400, "Invalid request");
      }

      auto messages_array = json_data["messages"];
      crow::json::wvalue response;
      response["results"] = crow::json::wvalue::list();

      int i = 0;
      for (auto &msg : messages_array)
      {
        try
        {
          std::string message = msg["message"].s();
          std::string signature = msg["signature"].s();
          std::string public_key = msg["public_key"].s();

          bool is_valid = verify_signature(message, signature, public_key);

          crow::json::wvalue result;
          result["verified"] = is_valid;
          response["results"][i] = std::move(result);
        } catch (const std::exception &e)
        {
          crow::json::wvalue error;
          error["error"] = e.what();
          response["results"][i] = std::move(error);
        }
        i++;
      }

      return crow::response(200, response);
    }
      catch (const std::exception &e)
      {
        return crow::response(500, e.what());
      } });

  // Endpoint de health check
  app.route_dynamic("/health")([&](const crow::request &req) -> crow::response
                               { return crow::response(200, "PQClean API is running"); });

  std::cout << "Server configured, starting on port 5003..." << std::endl;

  app.port(5003).multithreaded().concurrency(4).run();

  return 0;
}
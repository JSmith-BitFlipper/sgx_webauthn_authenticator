/*
 * Copyright (C) 2011-2018 Intel Corporation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Intel Corporation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */


#include <stdio.h>
#include <string.h>
#include <assert.h>

#include <fstream>

#include <unistd.h>
#include <pwd.h>

#include "sgx_urts.h"
#include "sgx_tcrypto.h"

#include "App.h"
#include "Enclave_u.h"

using namespace std;

// Function declarations
void fgets_nonewline(char *str, size_t n, FILE *stream);


#define MAX_PATH FILENAME_MAX
#define ENCLAVE_DATA_FILE "enclave_data.seal"

/* Global EID shared by multiple threads */
sgx_enclave_id_t global_eid = 0;

typedef struct _sgx_errlist_t {
    sgx_status_t err;
    const char *msg;
    const char *sug; /* Suggestion */
} sgx_errlist_t;

/* Error code returned by sgx_create_enclave */
static sgx_errlist_t sgx_errlist[] = {
    {
        SGX_ERROR_UNEXPECTED,
        "Unexpected error occurred.",
        NULL
    },
    {
        SGX_ERROR_INVALID_PARAMETER,
        "Invalid parameter.",
        NULL
    },
    {
        SGX_ERROR_OUT_OF_MEMORY,
        "Out of memory.",
        NULL
    },
    {
        SGX_ERROR_ENCLAVE_LOST,
        "Power transition occurred.",
        "Please refer to the sample \"PowerTransition\" for details."
    },
    {
        SGX_ERROR_INVALID_ENCLAVE,
        "Invalid enclave image.",
        NULL
    },
    {
        SGX_ERROR_INVALID_ENCLAVE_ID,
        "Invalid enclave identification.",
        NULL
    },
    {
        SGX_ERROR_INVALID_SIGNATURE,
        "Invalid enclave signature.",
        NULL
    },
    {
        SGX_ERROR_OUT_OF_EPC,
        "Out of EPC memory.",
        NULL
    },
    {
        SGX_ERROR_NO_DEVICE,
        "Invalid SGX device.",
        "Please make sure SGX module is enabled in the BIOS, and install SGX driver afterwards."
    },
    {
        SGX_ERROR_MEMORY_MAP_CONFLICT,
        "Memory map conflicted.",
        NULL
    },
    {
        SGX_ERROR_INVALID_METADATA,
        "Invalid enclave metadata.",
        NULL
    },
    {
        SGX_ERROR_DEVICE_BUSY,
        "SGX device was busy.",
        NULL
    },
    {
        SGX_ERROR_INVALID_VERSION,
        "Enclave version was invalid.",
        NULL
    },
    {
        SGX_ERROR_INVALID_ATTRIBUTE,
        "Enclave was not authorized.",
        NULL
    },
    {
        SGX_ERROR_ENCLAVE_FILE_ACCESS,
        "Can't open enclave file.",
        NULL
    },
};

/* Check error conditions for loading enclave */
void print_error_message(sgx_status_t ret)
{
    size_t idx = 0;
    size_t ttl = sizeof sgx_errlist/sizeof sgx_errlist[0];

    for (idx = 0; idx < ttl; idx++) {
        if(ret == sgx_errlist[idx].err) {
            if(NULL != sgx_errlist[idx].sug)
                printf("Info: %s\n", sgx_errlist[idx].sug);
            printf("Error: %s\n", sgx_errlist[idx].msg);
            break;
        }
    }
    
    if (idx == ttl)
    	printf("Error code is 0x%X. Please refer to the \"Intel SGX SDK Developer Reference\" for more details.\n", ret);
}

/* Initialize the enclave:
 *   Step 1: try to retrieve the launch token saved by last transaction
 *   Step 2: call sgx_create_enclave to initialize an enclave instance
 *   Step 3: save the launch token if it is updated
 */
int initialize_enclave(void)
{
    char token_path[MAX_PATH] = {'\0'};
    sgx_launch_token_t token = {0};
    sgx_status_t ret = SGX_ERROR_UNEXPECTED;
    int updated = 0;
    
    /* Step 1: try to retrieve the launch token saved by last transaction 
     *         if there is no token, then create a new one.
     */
    /* try to get the token saved in $HOME */
    const char *home_dir = getpwuid(getuid())->pw_dir;
    
    if (home_dir != NULL && 
        (strlen(home_dir)+strlen("/")+sizeof(TOKEN_FILENAME)+1) <= MAX_PATH) {
        /* compose the token path */
        strncpy(token_path, home_dir, strlen(home_dir));
        strncat(token_path, "/", strlen("/"));
        strncat(token_path, TOKEN_FILENAME, sizeof(TOKEN_FILENAME)+1);
    } else {
        /* if token path is too long or $HOME is NULL */
        strncpy(token_path, TOKEN_FILENAME, sizeof(TOKEN_FILENAME));
    }

    FILE *fp = fopen(token_path, "rb");
    if (fp == NULL && (fp = fopen(token_path, "wb")) == NULL) {
        printf("Warning: Failed to create/open the launch token file \"%s\".\n", token_path);
    }

    if (fp != NULL) {
        /* read the token from saved file */
        size_t read_num = fread(token, 1, sizeof(sgx_launch_token_t), fp);
        if (read_num != 0 && read_num != sizeof(sgx_launch_token_t)) {
            /* if token is invalid, clear the buffer */
            memset(&token, 0x0, sizeof(sgx_launch_token_t));
            printf("Warning: Invalid launch token read from \"%s\".\n", token_path);
        }
    }
    /* Step 2: call sgx_create_enclave to initialize an enclave instance */
    /* Debug Support: set 2nd parameter to 1 */
    ret = sgx_create_enclave(ENCLAVE_FILENAME, SGX_DEBUG_FLAG, &token, &updated, &global_eid, NULL);
    if (ret != SGX_SUCCESS) {
        print_error_message(ret);
        if (fp != NULL) fclose(fp);
        return -1;
    }

    /* Step 3: save the launch token if it is updated */
    if (updated == FALSE || fp == NULL) {
        /* if the token is not updated, or file handler is invalid, do not perform saving */
        if (fp != NULL) fclose(fp);
        return 0;
    }

    /* reopen the file with write capablity */
    fp = freopen(token_path, "wb", fp);
    if (fp == NULL) return 0;
    size_t write_num = fwrite(token, 1, sizeof(sgx_launch_token_t), fp);
    if (write_num != sizeof(sgx_launch_token_t))
        printf("Warning: Failed to save launch token to \"%s\".\n", token_path);
    fclose(fp);
    return 0;
}

/* OCall untrusted functions */
void untrusted_print_string(const char *str) {
    /* Proxy/Bridge will check the length and null-terminate 
     * the input string to prevent buffer overflow. 
     */
    printf("%s", str);

    return;
}

void untrusted_get_user_input(char *ret_str, size_t n) {
  fgets_nonewline(ret_str, n, stdin);
  return;
}

int32_t untrusted_save_enclave_data(const uint8_t *sealed_data, const size_t sealed_size) {
  ofstream file(ENCLAVE_DATA_FILE, ios::out | ios::binary);
  if (file.fail()) {
    return 1;
  }

  file.write((const char*)sealed_data, sealed_size);
  file.close();
  return 0;
}

int32_t untrusted_load_enclave_data(uint8_t *sealed_data, const size_t sealed_size) {
  ifstream file(ENCLAVE_DATA_FILE, ios::in | ios::binary);
  if (file.fail()) {
    return 1;
  }
  file.read((char*)sealed_data, sealed_size);
  file.close();
  return 0;
}

uint32_t hex2buf(char data_to_sign[], uint8_t **ret) {  
  if (!data_to_sign) {
    return 0;
  }

  // Every two characters is a byte 
  const uint32_t nchars_to_sign = strlen(data_to_sign);
  
  // Sanity check an even number of characters to sign
  if (nchars_to_sign % 2) {
    return 0;
  }

  const char *pos = data_to_sign;

  const uint32_t ret_size = nchars_to_sign / 2;
  *ret = (uint8_t*)malloc(ret_size);

  /* WARNING: no sanitization or error-checking whatsoever */
  size_t count;
  for (count = 0; count < ret_size; count++) {
    sscanf(pos, "%2hhx", *ret + count);
    pos += 2;
  }

  return ret_size;
}

// Perform an `fgets`, but trim the `\n` that gets added to the end
// when the user hits the return key to submit their response
void fgets_nonewline(char *str, size_t n, FILE *stream) {
    fgets(str, n, stream);

    // The `fgets` may add a newline at the end of the input, trim that
    const uint32_t str_len = strlen(str) - 1;
    if (str[str_len] == '\n') {
      str[str_len] = '\0';
    }
  
    return;
}

/* Application entry */
int SGX_CDECL main(int argc, char *argv[])
{
    (void)(argc);
    (void)(argv);

    /* Initialize the enclave */
    if(initialize_enclave() < 0) {
        printf("Failed to initialize enclave!\n");
        printf("Enter a character before exit ...\n");
        getchar();
        return -1; 
    }
 
    sgx_status_t status;
    int32_t i;

    sgx_ec256_public_t pk;
    get_public_key(global_eid, &status, &pk);

    if (status) {
      printf("App Error: %d!\n", status);
      return -1;
    }

    // Print the public key
    printf("Public Key:\n");

    // Reverse order because SGX stores it in little-endian and
    // python reads it as a human (big-endian) integer
    printf("gx: ");
    for (i = SGX_ECP256_KEY_SIZE - 1; i >= 0; i--) {
      printf("%02x", pk.gx[i]);
    }
    printf("\n");

    printf("gy: ");
    for (i = SGX_ECP256_KEY_SIZE - 1; i >= 0; i--) {
      printf("%02x", pk.gy[i]);
    }
    printf("\n");


    // Ensure some spacing
    printf("\n\n");


    // Get the client data for this attestation request
    const uint32_t client_data_json_size = 1024;
    char client_data_json[client_data_json_size];

    printf("Enter client JSON data:\n");
    fgets_nonewline(client_data_json, client_data_json_size, stdin);
    printf("\n");

    // Get user input as to what to sign
    const uint32_t data_to_sign_size = 256;
    char data_to_sign[data_to_sign_size];

    printf("Enter hex data to sign:\n");
    fgets_nonewline(data_to_sign, data_to_sign_size, stdin);
    printf("\n");

    // Decode the input into a byte array
    uint8_t *bytes_to_sign;
    const uint32_t nbytes_to_sign = hex2buf(data_to_sign, &bytes_to_sign);

    // Error check
    if (!nbytes_to_sign) {
      printf("Error receiving data to sign!\n");
      return -1;
    }

    sgx_ec256_signature_t signature;
    webauthn_get_signature(global_eid, &status, 
                           bytes_to_sign, nbytes_to_sign,
                           (const uint8_t*)client_data_json, client_data_json_size,
                           &signature);

    // Release the input bytes decoded arrays
    free(bytes_to_sign);

    // Check for errors
    if (status) {
      printf("Signature Error: %d!\n", status);
      return -1;
    }

    // Print the x and y coordinates of the signature
    printf("Resulting signature: ");

    // Reverse order because SGX stores it in little-endian and
    // python reads it as a human (big-endian) integer
    for (i = SGX_NISTP_ECP256_KEY_SIZE - 1; i >= 0; i--) {
      printf("%08x", signature.x[i]);
    }

    // Comma separate the x and y coordinates
    printf(",");

    for (i = SGX_NISTP_ECP256_KEY_SIZE - 1; i >= 0; i--) {
      printf("%08x", signature.y[i]);
    }
    printf("\n");    

    /* Destroy the enclave */
    sgx_destroy_enclave(global_eid);
    
    return 0;
}


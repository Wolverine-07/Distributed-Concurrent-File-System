#pragma once
#include "common.h"

// Helper to check for sentence delimiter
int is_delimiter(char c);

// Splits content into an array of sentences.
char** split_into_sentences(const char* content, int* sentence_count);

// Splits a single sentence into an array of words.
char** split_into_words(const char* sentence, int* word_count);

// Joins an array of words back into a single sentence string.
char* join_words(char** words, int word_count);

// Joins an array of sentences back into the full file content.
char* join_sentences(char** sentences, int sentence_count);

// The main function for the WRITE operation.
// Applies a single update (inserting content at a word index) to the file content.
// Returns a new, dynamically allocated string with the updated content.
char* apply_single_update(const char* current_content, int sent_num, int word_idx, const char* new_content);
#include "file_parser.h"
#include <ctype.h>

// Helper to check for sentence delimiter
int is_delimiter(char c) {
    return (c == '.' || c == '!' || c == '?');
}

// This is a complex parser. It splits by delimiter.
char** split_into_sentences(const char* content, int* sentence_count) {
    *sentence_count = 0;
    char** sentences = NULL;
    const char* start = content;
    const char* p = content;
    
    while (*p) {
        if (is_delimiter(*p)) {
            (*sentence_count)++;
            sentences = realloc(sentences, (*sentence_count) * sizeof(char*));
            int len = p - start + 1;
            sentences[*sentence_count - 1] = (char*)malloc(len + 1);
            strncpy(sentences[*sentence_count - 1], start, len);
            sentences[*sentence_count - 1][len] = '\0';
            start = p + 1;
            // Skip trailing whitespace
            while (*start && isspace(*start)) start++;
        }
        p++;
    }
    
    // Add any remaining text as the last sentence
    if (start < p && strlen(start) > 0) {
        (*sentence_count)++;
        sentences = realloc(sentences, (*sentence_count) * sizeof(char*));
        int len = p - start;
        sentences[*sentence_count - 1] = (char*)malloc(len + 1);
        strncpy(sentences[*sentence_count - 1], start, len);
        sentences[*sentence_count - 1][len] = '\0';
    }
    
    return sentences;
}

// --- MODIFIED: Intelligent Word Splitting ---
// Treats delimiters as separate words (e.g., "line." becomes "line", ".")
char** split_into_words(const char* sentence, int* word_count) {
    *word_count = 0;
    char** words = NULL;
    const char* start = sentence;
    const char* p = sentence;
    int in_word = 0;

    while (*p) {
        if (isspace(*p)) {
            if (in_word) {
                (*word_count)++;
                words = realloc(words, (*word_count) * sizeof(char*));
                int len = p - start;
                words[*word_count - 1] = (char*)malloc(len + 1);
                strncpy(words[*word_count - 1], start, len);
                words[*word_count - 1][len] = '\0';
                in_word = 0;
            }
            p++; 
            start = p;
        } else if (is_delimiter(*p)) {
            // If we were inside a word (e.g., "line."), finish "line" first
            if (in_word) {
                (*word_count)++;
                words = realloc(words, (*word_count) * sizeof(char*));
                int len = p - start;
                words[*word_count - 1] = (char*)malloc(len + 1);
                strncpy(words[*word_count - 1], start, len);
                words[*word_count - 1][len] = '\0';
                in_word = 0;
            }
            
            // Now handle the delimiter as its own standalone word
            (*word_count)++;
            words = realloc(words, (*word_count) * sizeof(char*));
            words[*word_count - 1] = (char*)malloc(2); // 1 char + null terminator
            words[*word_count - 1][0] = *p;
            words[*word_count - 1][1] = '\0';
            
            p++; 
            start = p;
        } else {
            in_word = 1;
            p++;
        }
    }
    
    // Trailing word check (e.g., if file doesn't end with delimiter/space)
    if (in_word) {
        (*word_count)++;
        words = realloc(words, (*word_count) * sizeof(char*));
        int len = p - start;
        words[*word_count - 1] = (char*)malloc(len + 1);
        strncpy(words[*word_count - 1], start, len);
        words[*word_count - 1][len] = '\0';
    }
    
    return words;
}

// --- MODIFIED: Intelligent Word Joining ---
// Avoids adding spaces before delimiters
char* join_words(char** words, int word_count) {
    if (word_count == 0) return strdup("");
    
    int total_len = 0;
    for (int i = 0; i < word_count; i++) {
        total_len += strlen(words[i]) + 1; 
    }
    
    char* sentence = (char*)calloc(total_len + 1, 1); // +1 safety
    for (int i = 0; i < word_count; i++) {
        strcat(sentence, words[i]);
        
        // Add space ONLY if:
        // 1. It's not the last word
        // 2. The NEXT word is NOT a delimiter (e.g., don't add space before ".")
        if (i < word_count - 1) {
            char next_first_char = words[i+1][0];
            if (!is_delimiter(next_first_char)) {
                strcat(sentence, " ");
            }
        }
    }
    return sentence;
}

char* join_sentences(char** sentences, int sentence_count) {
    if (sentence_count == 0) return strdup("");
    
    int total_len = 0;
    for (int i = 0; i < sentence_count; i++) {
        total_len += strlen(sentences[i]) + 1; // +1 for space
    }
    
    char* content = (char*)calloc(total_len, 1);
    for (int i = 0; i < sentence_count; i++) {
        strcat(content, sentences[i]);
        if (i < sentence_count - 1) {
            // Add space if the next sentence doesn't start with one
            if (sentences[i+1][0] != ' ') {
                strcat(content, " ");
            }
        }
    }
    return content;
}

char* apply_single_update(const char* current_content, int sent_num, int word_idx, const char* new_content) {
    int sentence_count = 0;
    char** sentences = split_into_sentences(current_content, &sentence_count);

    // Check for out-of-bounds, but allow appending
    if (sent_num < 0 || sent_num > sentence_count) {
        free_split_string(sentences, sentence_count);
        return NULL; 
    }

    if (sent_num == sentence_count) {
        // APPEND case
        if (sentence_count == 0 && sent_num == 0) {
            sentences = realloc(sentences, 1 * sizeof(char*));
            sentences[0] = strdup("");
            sentence_count = 1;
        } else {
            sentences = realloc(sentences, (sentence_count + 1) * sizeof(char*));
            sentences[sentence_count] = strdup("");
            sentence_count++;
        }
    }
    
    int word_count = 0;
    char** words = split_into_words(sentences[sent_num], &word_count);

    if (word_idx < 0 || word_idx > word_count) {
        free_split_string(words, word_count);
        free_split_string(sentences, sentence_count);
        return NULL;
    }

    // Insert new content
    int new_word_count = 0;
    char** new_words = split_string(new_content, " ", &new_word_count);

    char** final_words = (char**)calloc(word_count + new_word_count, sizeof(char*));
    int k = 0;
    for (int i = 0; i < word_idx; i++) final_words[k++] = strdup(words[i]);
    for (int i = 0; i < new_word_count; i++) final_words[k++] = strdup(new_words[i]);
    for (int i = word_idx; i < word_count; i++) final_words[k++] = strdup(words[i]);

    free_split_string(words, word_count);
    free_split_string(new_words, new_word_count);
    
    char* new_sentence = join_words(final_words, k);
    free_split_string(final_words, k);
    
    free(sentences[sent_num]);
    sentences[sent_num] = new_sentence;
    
    char* final_content = join_sentences(sentences, sentence_count);
    free_split_string(sentences, sentence_count);

    // Re-parse to handle new delimiters
    int final_sent_count = 0;
    char** final_sentences = split_into_sentences(final_content, &final_sent_count);
    free(final_content);
    
    char* final_final_content = join_sentences(final_sentences, final_sent_count);
    free_split_string(final_sentences, final_sent_count);
    
    return final_final_content;
}
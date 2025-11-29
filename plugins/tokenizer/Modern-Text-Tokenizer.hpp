/*
 * Modern-Text-Tokenizer.hpp
 * -------------------------------------
 * UTF-8 aware, high-performance C++ text tokenizer with vocabulary support.
 *
 * Author: Mecanik1337 (https://mecanik.dev/en/)
 * Repository: https://github.com/Mecanik/Modern-Text-Tokenizer
 * License: MIT License
 *
 * Description:
 *  This header-only library provides a fast and configurable tokenizer for
 *  modern C++17/20 projects. It supports Unicode-aware tokenization, vocabulary
 *  encoding/decoding, and transformer-ready input formatting (e.g. for BERT).
 *
 * Features:
 *  - Zero-copy tokenization using std::string_view
 *  - UTF-8 safe without external dependencies
 *  - Optional vocabulary loading/saving
 *  - Special token handling ([CLS], [SEP], etc.)
 *  - Fluent configuration API
 *  - Lightweight and portable (no Boost/ICU required)
 *
 * Designed for use in:
 *  - Natural Language Processing (NLP)
 *  - Machine Learning pipelines
 *  - Embedded or performance-sensitive applications
 *
 * See README for usage examples, benchmarks, and integration tips.
 *
 * Contributions welcome.
 * ---------------------------------------------------------------------------
 */

#pragma once
#include <string>
#include <string_view>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <algorithm>
#include <cctype>
#include <iostream>
#include <fstream>
#include <sstream>

namespace MecanikDev
{
	class TextTokenizer
	{
	private:
		std::unordered_set<char> delimiters_;
		bool lowercase_;
		bool keep_punctuation_;
		bool split_on_punctuation_;

		// Vocabulary support
		std::unordered_map<std::string, int> vocab_to_id_;
		std::vector<std::string> id_to_vocab_;
		std::string unk_token_;
		std::string pad_token_;
		std::string cls_token_;
		std::string sep_token_;
		bool use_vocab_;
		int unk_id_;
		int pad_id_;
		int cls_id_;
		int sep_id_;

		// UTF-8 helper functions
		static bool is_utf8_start(unsigned char c) {
			return (c & 0x80) == 0 || (c & 0xE0) == 0xC0 ||
				(c & 0xF0) == 0xE0 || (c & 0xF8) == 0xF0;
		}

		static size_t utf8_char_length(unsigned char c) {
			if ((c & 0x80) == 0) return 1;      // 0xxxxxxx
			if ((c & 0xE0) == 0xC0) return 2;   // 110xxxxx
			if ((c & 0xF0) == 0xE0) return 3;   // 1110xxxx
			if ((c & 0xF8) == 0xF0) return 4;   // 11110xxx
			// Invalid, treat as single byte
			return 1;
		}

		// Check if character is ASCII punctuation
		static bool is_ascii_punct(char c) {
			return std::ispunct(static_cast<unsigned char>(c));
		}

		// Safe lowercase for ASCII
		static char to_ascii_lower(char c) {
			return std::tolower(static_cast<unsigned char>(c));
		}

		// Normalize a token (lowercase if enabled)
		std::string normalize_token(std::string_view token) const {
			if (!lowercase_) {
				return std::string(token);
			}

			std::string result;
			result.reserve(token.size());

			for (size_t i = 0; i < token.size(); ) {
				unsigned char c = token[i];

				if ((c & 0x80) == 0) {
					// ASCII character - safe to lowercase
					result += to_ascii_lower(c);
					i++;
				}
				else {
					// Multi-byte UTF-8 - copy as-is
					size_t len = utf8_char_length(c);
					for (size_t j = 0; j < len && i + j < token.size(); j++) {
						result += token[i + j];
					}
					i += len;
				}
			}
			return result;
		}

		// Check if we should split at this position
		bool should_split_at(char c) const {
			return delimiters_.count(c) > 0 ||
				(split_on_punctuation_ && is_ascii_punct(c));
		}

	public:
		TextTokenizer()
			: delimiters_{ ' ', '\t', '\n', '\r', '\f', '\v' }
			, lowercase_(false)
			, keep_punctuation_(false)
			, split_on_punctuation_(false)
			, unk_token_("[UNK]")
			, pad_token_("[PAD]")
			, cls_token_("[CLS]")
			, sep_token_("[SEP]")
			, use_vocab_(false)
			, unk_id_(-1)
			, pad_id_(-1)
			, cls_id_(-1)
			, sep_id_(-1) {
		}

		// Configuration methods
		TextTokenizer& set_lowercase(bool enable) {
			lowercase_ = enable;
			return *this;
		}

		TextTokenizer& set_keep_punctuation(bool enable) {
			keep_punctuation_ = enable;
			return *this;
		}

		TextTokenizer& set_split_on_punctuation(bool enable) {
			split_on_punctuation_ = enable;
			return *this;
		}

		TextTokenizer& add_delimiter(char delim) {
			delimiters_.insert(delim);
			return *this;
		}

		TextTokenizer& add_delimiters(const std::string& delims) {
			for (char c : delims) {
				delimiters_.insert(c);
			}
			return *this;
		}

		// Vocabulary configuration methods
		TextTokenizer& set_special_tokens(const std::string& unk = "[UNK]",
			const std::string& pad = "[PAD]",
			const std::string& cls = "[CLS]",
			const std::string& sep = "[SEP]") {
			unk_token_ = unk;
			pad_token_ = pad;
			cls_token_ = cls;
			sep_token_ = sep;
			return *this;
		}

		// Load vocabulary from file
		bool load_vocab(const std::string& vocab_file) {
			std::ifstream file(vocab_file);
			if (!file.is_open()) {
				return false;
			}

			vocab_to_id_.clear();
			id_to_vocab_.clear();

			std::string token;
			int id = 0;

			while (std::getline(file, token)) {
				// Remove trailing whitespace
				token.erase(token.find_last_not_of(" \t\r\n") + 1);

				if (!token.empty()) {
					vocab_to_id_[token] = id;
					id_to_vocab_.push_back(token);

					// Store special token IDs
					if (token == unk_token_) unk_id_ = id;
					else if (token == pad_token_) pad_id_ = id;
					else if (token == cls_token_) cls_id_ = id;
					else if (token == sep_token_) sep_id_ = id;

					id++;
				}
			}

			use_vocab_ = true;
			return true;
		}

		// Create vocabulary from tokenized text
		TextTokenizer& build_vocab_from_text(const std::vector<std::string>& texts,
			int min_frequency = 1,
			int max_vocab_size = 50000) {
			std::unordered_map<std::string, int> token_counts;

			// Count token frequencies
			for (const auto& text : texts) {
				auto tokens = tokenize(text);
				for (const auto& token : tokens) {
					token_counts[token]++;
				}
			}

			// Sort by frequency
			std::vector<std::pair<std::string, int>> sorted_tokens;
			for (const auto& pair : token_counts) {
				if (pair.second >= min_frequency) {
					sorted_tokens.push_back(pair);
				}
			}

			std::sort(sorted_tokens.begin(), sorted_tokens.end(),
				[](const auto& a, const auto& b) {
					return a.second > b.second;
				});

			// Build vocabulary
			vocab_to_id_.clear();
			id_to_vocab_.clear();

			// Add special tokens first
			std::vector<std::string> special_tokens = { pad_token_, unk_token_, cls_token_, sep_token_ };
			for (const auto& token : special_tokens) {
				if (vocab_to_id_.find(token) == vocab_to_id_.end()) {
					int id = static_cast<int>(id_to_vocab_.size());
					vocab_to_id_[token] = id;
					id_to_vocab_.push_back(token);

					if (token == unk_token_) unk_id_ = id;
					else if (token == pad_token_) pad_id_ = id;
					else if (token == cls_token_) cls_id_ = id;
					else if (token == sep_token_) sep_id_ = id;
				}
			}

			// Add regular tokens
			int added = 0;
			for (const auto& pair : sorted_tokens) {
				if (vocab_to_id_.find(pair.first) == vocab_to_id_.end() &&
					added < max_vocab_size - static_cast<int>(special_tokens.size())) {
					int id = static_cast<int>(id_to_vocab_.size());
					vocab_to_id_[pair.first] = id;
					id_to_vocab_.push_back(pair.first);
					added++;
				}
			}

			use_vocab_ = true;
			return *this;
		}

		// Save vocabulary to file
		bool save_vocab(const std::string& vocab_file) const {
			if (!use_vocab_) return false;

			std::ofstream file(vocab_file);
			if (!file.is_open()) return false;

			for (const auto& token : id_to_vocab_) {
				file << token << "\n";
			}

			return true;
		}

		// Main tokenization method
		std::vector<std::string> tokenize(std::string_view text) const {
			std::vector<std::string> tokens;

			size_t start = 0;
			size_t i = 0;

			while (i < text.size()) {
				unsigned char c = text[i];

				// Handle UTF-8 multibyte characters
				if ((c & 0x80) != 0) {
					size_t char_len = utf8_char_length(c);
					i += char_len;
					continue;
				}

				// ASCII character - check if we should split
				if (should_split_at(c)) {
					// Add token if we have content
					if (i > start) {
						auto token_view = text.substr(start, i - start);
						if (!token_view.empty()) {
							tokens.push_back(normalize_token(token_view));
						}
					}

					// Add punctuation as separate token if keeping it
					if (keep_punctuation_ && is_ascii_punct(c)) {
						tokens.push_back(normalize_token(text.substr(i, 1)));
					}

					// Skip whitespace, find next non-delimiter
					while (i < text.size() && should_split_at(text[i])) {
						// If we're keeping punctuation, add each punct char
						if (keep_punctuation_ && is_ascii_punct(text[i]) &&
							i > start + (i - start > 0 ? 1 : 0)) {
							tokens.push_back(normalize_token(text.substr(i, 1)));
						}
						i++;
					}
					start = i;
				}
				else {
					i++;
				}
			}

			// Add final token if any
			if (start < text.size()) {
				auto token_view = text.substr(start);
				if (!token_view.empty()) {
					tokens.push_back(normalize_token(token_view));
				}
			}

			return tokens;
		}

		// Tokenize and return token IDs
		std::vector<int> encode(std::string_view text) const {
			auto tokens = tokenize(text);
			std::vector<int> ids;
			ids.reserve(tokens.size());

			if (!use_vocab_) {
				// If no vocabulary, just return indices based on order
				for (size_t i = 0; i < tokens.size(); ++i) {
					ids.push_back(static_cast<int>(i));
				}
				return ids;
			}

			for (const auto& token : tokens) {
				auto it = vocab_to_id_.find(token);
				if (it != vocab_to_id_.end()) {
					ids.push_back(it->second);
				}
				else {
					ids.push_back(unk_id_);
				}
			}

			return ids;
		}

		// Decode token IDs back to text
		std::string decode(const std::vector<int>& ids) const {
			if (!use_vocab_) return "";

			std::ostringstream result;
			bool first = true;

			for (int id : ids) {
				if (id >= 0 && id < static_cast<int>(id_to_vocab_.size())) {
					const std::string& token = id_to_vocab_[id];

					// Skip special tokens in output (except for debugging)
					if (token == pad_token_) continue;

					if (!first) result << " ";
					result << token;
					first = false;
				}
			}

			return result.str();
		}

		// Encode with special tokens for sequence classification
		std::vector<int> encode_sequence(std::string_view text,
			int max_length = 512,
			bool add_special_tokens = true) const {
			auto token_ids = encode(text);

			if (!add_special_tokens || !use_vocab_) {
				// Truncate if necessary
				if (static_cast<int>(token_ids.size()) > max_length) {
					token_ids.resize(max_length);
				}
				return token_ids;
			}

			std::vector<int> result;

			// Add CLS token at beginning
			if (cls_id_ >= 0) {
				result.push_back(cls_id_);
				max_length--;
			}

			// Add tokens (truncate if necessary)
			int available_length = max_length - (sep_id_ >= 0 ? 1 : 0);
			for (int i = 0; i < std::min(static_cast<int>(token_ids.size()), available_length); ++i) {
				result.push_back(token_ids[i]);
			}

			// Add SEP token at end
			if (sep_id_ >= 0) {
				result.push_back(sep_id_);
			}

			return result;
		}

		// Get vocabulary size
		size_t vocab_size() const {
			return use_vocab_ ? id_to_vocab_.size() : 0;
		}

		// Get special token IDs
		int get_unk_id() const { return unk_id_; }
		int get_pad_id() const { return pad_id_; }
		int get_cls_id() const { return cls_id_; }
		int get_sep_id() const { return sep_id_; }

		std::string get_token_by_id(int id) const {
			if (!use_vocab_ || id < 0 || id >= static_cast<int>(id_to_vocab_.size())) {
				return "[INVALID]";
			}
			return id_to_vocab_[id];
		}

		// Check if using vocabulary
		bool has_vocab() const { return use_vocab_; }

		// Convenience method for simple whitespace tokenization
		static std::vector<std::string> simple_split(std::string_view text) {
			return TextTokenizer().tokenize(text);
		}

		// Method to get token count without storing tokens
		size_t count_tokens(std::string_view text) const {
			size_t count = 0;
			size_t start = 0;
			size_t i = 0;

			while (i < text.size()) {
				unsigned char c = text[i];

				if ((c & 0x80) != 0) {
					i += utf8_char_length(c);
					continue;
				}

				if (should_split_at(c)) {
					if (i > start) count++;

					if (keep_punctuation_ && is_ascii_punct(c)) count++;

					while (i < text.size() && should_split_at(text[i])) {
						if (keep_punctuation_ && is_ascii_punct(text[i]) &&
							i > start + (i - start > 0 ? 1 : 0)) {
							count++;
						}
						i++;
					}
					start = i;
				}
				else {
					i++;
				}
			}

			if (start < text.size()) count++;
			return count;
		}
	};
}
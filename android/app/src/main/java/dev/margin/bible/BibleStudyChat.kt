// SPDX-License-Identifier: MIT
package dev.margin.bible

import dev.margin.protocol.Tag
import dev.margin.protocol.Tlv

/**
 * Bible study chat integration: builds prompts from Bible passage context.
 *
 * STATUS: stub. This will integrate with ChatViewModel to handle BIBLE_CONTEXT
 * messages from the device and build appropriate LLM prompts.
 *
 * Usage flow:
 * 1. User is reading John 3:16 on the device
 * 2. User triggers "Ask about this verse" action
 * 3. Device sends BIBLE_CONTEXT with ref, book name, chapter, verse, text
 * 4. App builds a prompt like "Explain John 3:16: For God so loved..."
 * 5. LLM response streams back via CHAT_DELTA
 *
 * @see docs/bible-mode.md Phase 4: BLE Study Chat
 */
object BibleStudyChat {

    data class PassageContext(
        val ref: String,          // OSIS reference, e.g. "JHN.3.16"
        val bookName: String,     // Full book name, e.g. "John"
        val chapter: Int,         // Chapter number
        val verse: Int,           // Verse number (0 if whole chapter)
        val text: String,         // The verse/passage text
    ) {
        /** Format as human-readable reference, e.g. "John 3:16" */
        fun humanRef(): String = when {
            verse > 0 -> "$bookName $chapter:$verse"
            else -> "$bookName $chapter"
        }
    }

    /**
     * Parse a BIBLE_CONTEXT message body into PassageContext.
     */
    fun parseContext(body: ByteArray): PassageContext? {
        var ref = ""; var bookName = ""; var chapter = 0; var verse = 0; var text = ""
        for (it in Tlv.parse(body)) when (it.tag) {
            Tag.BIBLE_REF -> ref = it.str()
            Tag.BIBLE_BOOK_NAME -> bookName = it.str()
            Tag.BIBLE_CHAPTER -> chapter = it.u16()
            Tag.BIBLE_VERSE -> verse = it.u16()
            Tag.TEXT -> text = it.str()
        }
        if (ref.isEmpty() || text.isEmpty()) return null
        return PassageContext(ref, bookName, chapter, verse, text)
    }

    /**
     * Build a study prompt from passage context.
     *
     * The prompt includes:
     * - The action (explain, define, etc.)
     * - The passage reference
     * - The verse text
     * - Optional system context for the LLM
     */
    fun buildStudyPrompt(context: PassageContext, action: StudyAction = StudyAction.EXPLAIN): String {
        val ref = context.humanRef()
        return when (action) {
            StudyAction.EXPLAIN -> """
                Explain this Bible verse in simple terms:
                
                $ref: "${context.text}"
                
                Help me understand what this verse means, its context in the chapter, and how it applies today.
            """.trimIndent()

            StudyAction.DEFINE -> """
                Define the key terms and concepts in this Bible verse:
                
                $ref: "${context.text}"
                
                Explain important words, their original meanings, and any theological significance.
            """.trimIndent()

            StudyAction.CONTEXT -> """
                Provide historical and literary context for this Bible verse:
                
                $ref: "${context.text}"
                
                Include information about when it was written, to whom, and what was happening at the time.
            """.trimIndent()

            StudyAction.CROSS_REFERENCES -> """
                What other Bible verses relate to this passage?
                
                $ref: "${context.text}"
                
                List related verses with brief explanations of how they connect.
            """.trimIndent()

            StudyAction.APPLICATION -> """
                How can I apply this Bible verse to my life today?
                
                $ref: "${context.text}"
                
                Give practical ways to live out what this verse teaches.
            """.trimIndent()

            StudyAction.ASK -> """
                I have a question about this Bible verse:
                
                $ref: "${context.text}"
            """.trimIndent()
        }
    }

    /**
     * Build a system prompt for Bible study conversations.
     * This sets the context for the LLM to behave as a Bible study assistant.
     */
    fun buildSystemPrompt(): String = """
        You are a helpful Bible study assistant. When discussing Scripture:
        - Be accurate and faithful to the biblical text
        - Explain concepts clearly for readers of all backgrounds
        - Reference original languages (Hebrew/Greek) when helpful
        - Acknowledge different scholarly interpretations when relevant
        - Be respectful and encouraging
        - Keep responses concise for e-ink display (short paragraphs)
    """.trimIndent()

    enum class StudyAction {
        EXPLAIN,
        DEFINE,
        CONTEXT,
        CROSS_REFERENCES,
        APPLICATION,
        ASK,
    }
}

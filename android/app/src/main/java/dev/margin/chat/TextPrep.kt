// SPDX-License-Identifier: MIT
package dev.margin.chat

/**
 * "Pre-wrapping" for the e-ink screen. The phone does the expensive, fuzzy work so the reader only
 * runs a cheap greedy word-wrap with its real font metrics (the reader uses proportional fonts, so
 * exact line breaks can only be computed on the device):
 *  - strip Markdown the reader cannot render (emphasis, headings, links -> text, code fences),
 *  - turn lists and tables into plain lines,
 *  - collapse runs of blank lines and whitespace,
 *  - hard-break very long tokens (URLs, hashes) at the device's `columns` hint so a single word
 *    never overflows a line.
 *
 * TODO(M4): tables, nested lists, and keeping streaming deltas stable (a "**" split across two
 * deltas must not leak). Today this runs per delta and is only a first approximation.
 */
object TextPrep {
    private val bold = Regex("""\*\*(.+?)\*\*|__(.+?)__""")
    private val italic = Regex("""(?<![\w*])\*(?!\s)(.+?)(?<!\s)\*(?!\w)""")
    private val link = Regex("""\[([^\]]+)]\(([^)]+)\)""")
    private val heading = Regex("""(?m)^#{1,6}\s+""")
    private val bullet = Regex("""(?m)^\s*[-*+]\s+""")
    private val fence = Regex("""(?m)^```.*$""")
    private val blankRuns = Regex("""\n{3,}""")

    fun forEink(markdown: String, columns: Int = 40): String {
        var s = markdown
        s = fence.replace(s, "")
        s = heading.replace(s, "")
        s = bullet.replace(s, "• ")
        s = link.replace(s) { it.groupValues[1] }
        s = bold.replace(s) { it.groupValues[1].ifEmpty { it.groupValues[2] } }
        s = italic.replace(s) { it.groupValues[1] }
        s = blankRuns.replace(s, "\n\n")
        return breakLongTokens(s, columns.coerceAtLeast(8))
    }

    private fun breakLongTokens(s: String, columns: Int): String =
        s.split(' ').joinToString(" ") { tok -> if (tok.length > columns) tok.chunked(columns).joinToString(" ") else tok }
}

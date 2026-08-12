---
name: youyeetoo-protocol-audit
description: Reconcile Youyeetoo project requirements and interface-control documents across PDF, DOCX, XLSX, Markdown, and source code. Use for protocol extraction, IP and port planning, CAN or Ethernet/NB-IoT/XTC/XDT requirement comparison, contradiction analysis, demo-versus-formal baseline review, test-matrix preparation, and evidence-backed requirement reports.
---

# Youyeetoo Protocol Audit

## Audit Method

1. Inventory all source artifacts and record filename, version, date, and authority.
2. Use the appropriate document or spreadsheet Skill to extract text or tables when the source is binary.
3. Normalize claims into fields: interface, address, port, transport, frame, length, checksum, retry, rate, timeout, and acceptance test.
4. Label every claim `CONFIRMED`, `CONFLICT`, `MISSING`, `DEMO_ASSUMPTION`, or `STALE`.
5. Preserve page, section, table, or source-file citations for every nontrivial claim.
6. Produce an implementation boundary and a list of questions that require ICD or hardware-owner confirmation.

Declare `Agent A-ICD`. This role is analysis-first and must not silently modify production code or freeze undocumented values.

## Evidence Rules

- Prefer the newest approved requirement over older summaries, but do not erase historical facts.
- Treat a demo's IP, port, timeout, or packet format as provisional unless a formal source confirms it.
- Do not infer a missing port, MAC, MTU, retry policy, or throughput target from a convenient implementation value.
- Distinguish processor/platform mismatch from software behavior mismatch.
- Keep contradictions visible; resolve them with a decision record rather than silently choosing one value.

## Completion Output

Return a source inventory, normalized requirement matrix, contradiction table, confirmed baseline, demo assumptions, missing decisions, implementation impact, and test implications.

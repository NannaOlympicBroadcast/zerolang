import type { ReactNode } from "react";
import { DocsSidebarShell } from "@/components/docs-sidebar";
import { docs, groupBySection } from "@/lib/docs";

export default function DocsLayout({ children }: { children: ReactNode }) {
  const groups = groupBySection(docs);

  return (
    <div className="grid min-h-screen grid-cols-1 md:grid-cols-[15rem_minmax(0,1fr)] lg:grid-cols-[15rem_minmax(0,1fr)_14rem]">
      <DocsSidebarShell groups={groups} />
      {children}
    </div>
  );
}

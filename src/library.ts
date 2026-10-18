import type {Asset} from './model'

export interface SourceMapping {id?:string;notes?:Record<string,string|null>;rules?:({prefix:string;id?:string;notes?:Record<string,string|null>})[]}
export interface LibrarySource {id:string;path:string;publishedRun:string;state:string;scanned:number;total:number;mapping:SourceMapping;unsupported?:number;latestRun?:string;mappingVersion?:number;needsReimport?:boolean}
export interface LibraryProgress {state:string;running?:boolean;path?:string;runId?:string;scanned?:number;total?:number;midiFiles?:number;accepted?:number;ignored?:number;unsupported?:number;metadataFiles?:number;error?:string;stage?:string;completed?:number;groups?:number;descriptions?:number;totalDescriptions?:number}
export interface NeuralStatus extends LibraryProgress {indexId?:string;readyIndexId?:string;readyRevision?:number;catalogRevision?:number;stale?:boolean;preparationNeeded?:boolean}
export interface PatternFamily {id:string;name:string;query:string;canonicalQuery:string;numerator:number;denominator:number;feel:'straight'|'triplet'|'any';revision:number;members:number;ambiguous:number;threshold:number;queries?:string[]}
export interface FamilyStatus {revision:number;matchingRevision?:number;buildId:string;indexId?:string;policyVersion:number;state:string;stale:boolean;families:PatternFamily[];suggestions:number}
export interface FamilyMember {bindingId:string;patternId:string;path:string;score:number;state:'matched'|'ambiguous'|'included';decision:'include'|'auto';role:string;numerator:number;denominator:number;alternatives?:{familyId:string;score:number;state:string}[];moreAlternatives?:boolean}
export interface FamilyDecision {bindingId:string;patternId:string;path:string;decision:'include'|'exclude';numerator:number;denominator:number}
export interface FamilySuggestion {id:string;name:string;query:string;numerator:number;denominator:number;feel:'straight'|'triplet'|'any';patterns:number;supportedPatterns:number;cohesion:number;examples:{patternId:string;bindingId:string;path:string}[];overlap:{familyId:string;patterns:number}[]}
export interface LibraryStatus {catalog:{revision:number;patterns:number;sources:LibrarySource[]};progress:LibraryProgress;neural?:NeuralStatus;families?:FamilyStatus}
export interface LibraryRow {id:string;patternId:string;path:string;name:string;description:string;family:string;role:string;length:number;numerator:number;denominator:number;density?:number;coverage:number;features:{notes:number};similarity?:number;semanticDescription?:string}
export interface LibraryPage {rows:LibraryRow[];offset:number;more:boolean;revision:number;patterns:number}
export interface NeuralPage {rows:LibraryRow[];indexId:string;revision:number;provider:string;milliseconds:number;interpretedQuery?:string;queryVersion?:number}
export const libraryAsset=(row:LibraryRow):Asset=>({id:`library:${row.id}`,libraryId:row.id,path:row.path,name:row.name,folder:false,size:row.features.notes})
